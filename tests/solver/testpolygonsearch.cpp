#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <gtest/gtest.h>

#include "polygonfixture.h"

namespace
{
using namespace aipackaging::solver;
using namespace aipackaging::tests;
} // namespace

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
    const SearchProgressStage expectedStage = kind == SolverKind::RandomLeftBottom ? SearchProgressStage::RandomIterations
                                            : kind == SolverKind::Beam             ? SearchProgressStage::ExpandedStates
                                                                                   : SearchProgressStage::Instances;
    EXPECT_TRUE(std::all_of(progress.begin(), progress.end(), [expectedStage](const SearchProgress & value)
                            { return value.stage == expectedStage && value.completed <= value.total; }));
    EXPECT_FALSE(controlled.cancelled);
    EXPECT_EQ(controlled.solution.placements, reference.placements);
    EXPECT_EQ(controlled.solution.objective.usedLength, reference.objective.usedLength);
    EXPECT_EQ(controlled.solution.metrics.expandedStates, reference.metrics.expandedStates);
  }
}

/// Проверяет cooperative cancellation до первой мутации и валидность возвращённого partial.
TEST(PolygonSolver, CancelsAtSafeBoundary)
{
  for (const SolverKind kind : {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                SolverKind::RandomLeftBottom, SolverKind::Beam})
  {
    SolverConfig config;
    config.solver = kind;
    config.timeoutMs = 0;
    PolygonExecutionControl control;
    control.cancellationRequested = []()
    {
      return true;
    };
    const PolygonSolverExecutionResult result = runPolygonProblem(problem(), config, control);
    EXPECT_TRUE(result.cancelled) << toString(kind);
    EXPECT_TRUE(result.solution.placements.empty()) << toString(kind);
    EXPECT_TRUE(validatePolygonSolution(problem(), result.solution).success) << toString(kind);
  }
}

/// Проверяет возврат лучшего independently validated partial после отмены между экземплярами.
TEST(PolygonSolver, ReturnsBestPartialWhenCancelledAfterProgress)
{
  SolverConfig config;
  config.timeoutMs = 0;
  bool cancel = false;
  SearchExecutionControl control;
  control.cancellationRequested = [&cancel]()
  {
    return cancel;
  };
  control.progress = [&cancel](const SearchProgress &)
  {
    cancel = true;
  };

  const PolygonSolverExecutionResult result = runPolygonProblem(problem(), config, control);

  EXPECT_TRUE(result.cancelled);
  EXPECT_EQ(result.solution.placements.size(), 1);
  EXPECT_TRUE(validatePolygonSolution(problem(), result.solution).success);
}
