#include <memory>
#include <numbers>
#include <string>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_learning.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт замкнутый прямоугольный путь для независимых unit-тестов.
PolygonPath rectangle(double width, double height, double x = 0.0, double y = 0.0)
{
  PolygonPath result;
  result.start = {x, y};
  result.segments = {{PolygonSegmentKind::Line, {x + width, y}},
                     {PolygonSegmentKind::Line, {x + width, y + height}},
                     {PolygonSegmentKind::Line, {x, y + height}},
                     {PolygonSegmentKind::Line, {x, y}}};
  return result;
}

/// Создаёт минимальную валидную полигональную задачу с двумя экземплярами.
PolygonProblem problem()
{
  PolygonProblem result;
  result.problemId = "polygon-test";
  result.sheet = {100.0, 60.0, "mm"};
  result.manufacturing = {2.0, 1.0, 0.2, 0.05};
  PolygonPart part;
  part.id = "rectangle";
  part.quantity = 2;
  part.outer = rectangle(30.0, 20.0);
  part.allowedRotations = {0, 90, 180, 270};
  result.parts.push_back(std::move(part));
  return result;
}
} // namespace

/// Проверяет микронную нормализацию и дедупликацию симметричных поворотов.
TEST(PolygonEnvironment, NormalizesAndDeduplicatesRotations)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  EXPECT_EQ(environment->sheetWidth(), 100000);
  EXPECT_EQ(environment->partSpacing(), 1000);
  EXPECT_EQ(environment->orientations(0).size(), 2);
}

/// Проверяет, что отверстие сохраняет площадь, но не разрешает вложение детали.
TEST(PolygonEnvironment, PreservesHoleAndRejectsNestedPlacement)
{
  PolygonProblem value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].holes.push_back(rectangle(10.0, 10.0, 10.0, 5.0));
  PolygonPart inner;
  inner.id = "inner";
  inner.outer = rectangle(5.0, 5.0);
  inner.allowedRotations = {0};
  value.parts.push_back(inner);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 2000, 2000, 0}));
  EXPECT_FALSE(environment->canApply(state, {"inner", 0, 14000, 9000, 0}));
  EXPECT_EQ(environment->orientations(0).front().materialArea, 500000000ULL);
}

/// Проверяет разрешённое касание при нулевом зазоре и запрет при положительном.
TEST(PolygonEnvironment, EnforcesSpacingButAllowsZeroTouch)
{
  PolygonProblem value = problem();
  value.manufacturing.sheetMargin = 0.0;
  value.manufacturing.partSpacing = 0.0;
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 0, 0, 0}));
  EXPECT_TRUE(environment->canApply(state, {"rectangle", 1, 30000, 0, 0}));

  value.manufacturing.partSpacing = 1.0;
  environment = PolygonEnvironment::Create(value, error);
  state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"rectangle", 0, 0, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"rectangle", 1, 30000, 0, 0}));
  EXPECT_TRUE(environment->canApply(state, {"rectangle", 1, 31000, 0, 0}));
}

/// Проверяет контролируемую аппроксимацию круговых дуг и кубической Bézier.
TEST(PolygonEnvironment, ApproximatesArcAndBezierPaths)
{
  PolygonProblem value = problem();
  value.parts.clear();
  PolygonPart circle;
  circle.id = "circle";
  circle.outer.start = {0.0, 10.0};
  circle.outer.segments = {{PolygonSegmentKind::Arc, {20.0, 10.0}, {10.0, 10.0}, {}, {}, false},
                           {PolygonSegmentKind::Arc, {0.0, 10.0}, {10.0, 10.0}, {}, {}, false}};
  circle.allowedRotations = {0, 90};
  value.parts.push_back(circle);
  PolygonPart curved;
  curved.id = "bezier";
  curved.outer.start = {0.0, 0.0};
  curved.outer.segments = {{PolygonSegmentKind::Line, {20.0, 0.0}},
                           {PolygonSegmentKind::Line, {20.0, 10.0}},
                           {PolygonSegmentKind::CubicBezier, {0.0, 10.0}, {}, {15.0, 18.0}, {5.0, 18.0}},
                           {PolygonSegmentKind::Line, {0.0, 0.0}}};
  curved.allowedRotations = {0};
  value.parts.push_back(curved);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  EXPECT_GT(environment->orientations(0).front().outer.size(), 16);
  EXPECT_GT(environment->orientations(1).front().outer.size(), 4);
  EXPECT_NEAR(static_cast<double>(environment->orientations(0).front().materialArea), 100.0 * std::numbers::pi * 1000000.0,
              2100000.0);
}

/// Проверяет отказ от самопересечения и касающегося внешней границы отверстия.
TEST(PolygonEnvironment, RejectsInvalidTopology)
{
  PolygonProblem value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].outer.start = {0.0, 0.0};
  value.parts[0].outer.segments = {{PolygonSegmentKind::Line, {20.0, 20.0}},
                                   {PolygonSegmentKind::Line, {0.0, 20.0}},
                                   {PolygonSegmentKind::Line, {20.0, 0.0}},
                                   {PolygonSegmentKind::Line, {0.0, 0.0}}};
  std::string error;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);

  value = problem();
  value.parts[0].quantity = 1;
  value.parts[0].holes.push_back(rectangle(10.0, 5.0, 0.0, 5.0));
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
}

/// Проверяет детерминированность NFP-кандидатов и точные primary-компоненты.
TEST(PolygonEnvironment, GeneratesStableNfpCandidatesAndObjective)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  PolygonState state = environment->initialState();
  const auto first = environment->enumerateCandidates(state, 0);
  EXPECT_EQ(first, environment->enumerateCandidates(state, 0));
  ASSERT_TRUE(environment->apply(state, first.front()));
  const auto second = environment->enumerateCandidates(state, 1);
  EXPECT_EQ(second, environment->enumerateCandidates(state, 1));
  ASSERT_FALSE(second.empty());
  ASSERT_TRUE(environment->apply(state, second.front()));
  const PolygonObjectiveComponents objective = environment->evaluate(state);
  EXPECT_EQ(objective.usedLength, 30000);
  EXPECT_EQ(objective.primaryRemnantWidth, 66000);
  EXPECT_EQ(objective.placedParts, 2);
}

/// Проверяет сохранение точечного inner-fit кандидата при точном размере листа.
TEST(PolygonEnvironment, GeneratesCandidateForExactSheetFit)
{
  PolygonProblem value = problem();
  value.sheet = {30.0, 20.0, "mm"};
  value.manufacturing.sheetMargin = 0.0;
  value.manufacturing.partSpacing = 0.0;
  value.parts[0].quantity = 1;
  value.parts[0].allowedRotations = {0};
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(value, error);
  ASSERT_NE(environment, nullptr) << error;
  const auto candidates = environment->enumerateCandidates(environment->initialState(), 0);
  ASSERT_EQ(candidates.size(), 1);
  EXPECT_EQ(candidates.front(), (PolygonAction{"rectangle", 0, 0, 0, 0}));
}

/// Проверяет строгий JSON round-trip и отказ от неизвестного поля.
TEST(PolygonIo, RoundTripsProblemAndRejectsUnknownField)
{
  const std::string text = savePolygonProblemToText(problem());
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(text);
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.problem.problemId, "polygon-test");
  std::string invalid = text;
  invalid.insert(invalid.find('{') + 1, "\n  \"unknown\": 1,");
  EXPECT_FALSE(loadPolygonProblemFromText(invalid).success);
}

/// Проверяет, что независимый валидатор обнаруживает подмену objective.
TEST(PolygonIo, RejectsMismatchedSolutionObjective)
{
  PolygonSolution solution = solvePolygonProblem(problem());
  ASSERT_TRUE(solution.complete());
  ++solution.objective.usedLength;
  EXPECT_FALSE(validatePolygonSolution(problem(), solution).success);
}

/// Проверяет все baseline и независимый валидатор на простой задаче.
TEST(PolygonSolver, SolvesAndValidatesEveryBaseline)
{
  for (const SolverKind kind : {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                SolverKind::RandomLeftBottom, SolverKind::Beam})
  {
    SolverConfig config;
    config.solver = kind;
    config.randomIterations = 4;
    config.beamWidth = 4;
    config.maxExpandedStates = 100;
    config.timeoutMs = 0;
    const PolygonSolution solution = solvePolygonProblem(problem(), config);
    ASSERT_TRUE(solution.complete()) << toString(kind) << ": " << solution.errorMessage;
    EXPECT_TRUE(validatePolygonSolution(problem(), solution).success);
    const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromText(savePolygonSolutionToText(solution));
    ASSERT_TRUE(loaded.success) << loaded.error;
    EXPECT_TRUE(validatePolygonSolution(problem(), loaded.solution).success);
  }
}

/// Проверяет сохранение лучшего partial всеми baseline на несовместимой вместимости.
TEST(PolygonSolver, ReturnsValidatedPartialForEveryBaseline)
{
  PolygonProblem value = problem();
  value.sheet = {30.0, 20.0, "mm"};
  value.manufacturing.sheetMargin = 0.0;
  value.manufacturing.partSpacing = 0.0;
  value.parts[0].allowedRotations = {0};
  for (const SolverKind kind : {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                SolverKind::RandomLeftBottom, SolverKind::Beam})
  {
    SolverConfig config;
    config.solver = kind;
    config.randomIterations = 4;
    config.beamWidth = 4;
    config.maxExpandedStates = 100;
    config.timeoutMs = 0;
    const PolygonSolution solution = solvePolygonProblem(value, config);
    EXPECT_FALSE(solution.complete());
    EXPECT_EQ(solution.objective.placedParts, 1);
    EXPECT_TRUE(validatePolygonSolution(value, solution).success);
  }
}

/// Проверяет детерминированные placements и счётчики при одинаковом random seed.
TEST(PolygonSolver, RepeatsSeededSearch)
{
  SolverConfig config;
  config.solver = SolverKind::RandomLeftBottom;
  config.seed = 123456;
  config.randomIterations = 12;
  config.timeoutMs = 0;
  const PolygonSolution first = solvePolygonProblem(problem(), config);
  const PolygonSolution second = solvePolygonProblem(problem(), config);
  EXPECT_EQ(first.placements, second.placements);
  EXPECT_EQ(first.metrics.candidatesGenerated, second.metrics.candidatesGenerated);
  EXPECT_EQ(first.metrics.candidatesValidated, second.metrics.candidatesValidated);
  EXPECT_EQ(first.metrics.expandedStates, second.metrics.expandedStates);
}

/// Проверяет диагностируемое исчерпание детерминированного beam-бюджета.
TEST(PolygonSolver, ReportsBeamBudgetExhaustion)
{
  SolverConfig config;
  config.solver = SolverKind::Beam;
  config.maxExpandedStates = 1;
  config.beamWidth = 4;
  config.timeoutMs = 0;
  const PolygonSolution solution = solvePolygonProblem(problem(), config);
  EXPECT_EQ(solution.status, SolveStatus::BudgetExhausted);
  EXPECT_EQ(solution.metrics.expandedStates, 1);
  EXPECT_TRUE(validatePolygonSolution(problem(), solution).success);
}

/// Проверяет progress каждого baseline и отсутствие влияния callback на детерминированный результат.
TEST(PolygonSolver, ReportsProgressWithoutChangingSolution)
{
  for (const SolverKind kind : {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                SolverKind::RandomLeftBottom, SolverKind::Beam})
  {
    SolverConfig config;
    config.solver = kind;
    config.randomIterations = 4;
    config.beamWidth = 4;
    config.maxExpandedStates = 100;
    config.timeoutMs = 0;
    std::vector<PolygonSolverProgress> progress;
    PolygonExecutionControl control;
    control.progress = [&progress](const PolygonSolverProgress & value)
    {
      progress.push_back(value);
    };
    const PolygonSolution reference = solvePolygonProblem(problem(), config);
    const PolygonSolverExecutionResult controlled = runPolygonProblem(problem(), config, control);
    ASSERT_FALSE(progress.empty()) << toString(kind);
    EXPECT_FALSE(controlled.cancelled);
    EXPECT_EQ(controlled.solution.placements, reference.placements);
    EXPECT_EQ(controlled.solution.objective.usedLength, reference.objective.usedLength);
    EXPECT_EQ(controlled.solution.metrics.expandedStates, reference.metrics.expandedStates);
  }
}

/// Проверяет cooperative cancellation до первой мутации и валидность возвращённого partial.
TEST(PolygonSolver, CancelsAtSafeBoundary)
{
  SolverConfig config;
  config.timeoutMs = 0;
  PolygonExecutionControl control;
  control.cancellationRequested = []()
  {
    return true;
  };
  const PolygonSolverExecutionResult result = runPolygonProblem(problem(), config, control);
  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.solution.placements.empty());
  EXPECT_TRUE(validatePolygonSolution(problem(), result.solution).success);
}

/// Проверяет ограничения размера листа и числа обязательных экземпляров.
TEST(PolygonEnvironment, RejectsContractComplexityLimits)
{
  std::string error;
  PolygonProblem value = problem();
  value.sheet.width = 10000.001;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
  value = problem();
  value.parts[0].quantity = 101;
  EXPECT_EQ(PolygonEnvironment::Create(value, error), nullptr);
}

/// Проверяет, что слишком большое целое поворота отклоняется без исключения parser.
TEST(PolygonIo, RejectsOutOfRangeRotation)
{
  std::string text = savePolygonProblemToText(problem());
  const std::size_t rotations = text.find("\"allowedRotations\"");
  ASSERT_NE(rotations, std::string::npos);
  const std::size_t position = text.find('0', rotations);
  ASSERT_NE(position, std::string::npos);
  text.replace(position, 1, "9223372036854775807");
  EXPECT_FALSE(loadPolygonProblemFromText(text).success);
}

/// Проверяет динамический каталог, переход и неизменность после ошибочного индекса.
TEST(PolygonLearning, UsesDynamicActionsAndKeepsStateOnError)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  const std::size_t initialCount = environment->actions().size();
  ASSERT_GT(initialCount, 0);
  EXPECT_THROW(environment->step(initialCount), std::out_of_range);
  EXPECT_EQ(environment->actions().size(), initialCount);
  const PolygonLearningStepResult step = environment->step(0);
  EXPECT_GT(step.reward, 0.0);
  EXPECT_EQ(step.observation.occupied.size(), 128U * 128U);
  EXPECT_FALSE(environment->isComplete());
  const PolygonPlacementObservation placement = environment->placementObservation(1, 0);
  EXPECT_EQ(placement.channels.size(), 4U * 128U * 128U);
}

/// Проверяет terminal-контракт и запрет шага после полного решения.
TEST(PolygonLearning, TerminatesAndRejectsFurtherSteps)
{
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem(), error);
  ASSERT_NE(environment, nullptr) << error;
  while (!environment->isTerminal())
    environment->step(0);
  EXPECT_TRUE(environment->isComplete());
  EXPECT_THROW(environment->step(0), std::logic_error);
}
