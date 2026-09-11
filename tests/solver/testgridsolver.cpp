#include <algorithm>
#include <array>
#include <vector>

#include <aipackaging/nesting/grid_environment.h>
#include <aipackaging/nesting/grid_solver.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт небольшую задачу, которую обязаны решить все baseline-алгоритмы.
GridProblem commonProblem()
{
  GridProblem problem;
  problem.problemId = "common";
  problem.sheet = {4, 4, "cell"};
  problem.parts = {{"L", 1, {{0, 0}, {0, 1}, {1, 1}}, {0, 90, 180, 270}}, {"domino", 2, {{0, 0}, {1, 0}}, {0, 90}}};
  return problem;
}
} // namespace

TEST(GridSolver, EveryBaselineProducesIndependentlyValidSolution)
{
  constexpr std::array SOLVERS = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                  SolverKind::RandomLeftBottom, SolverKind::Beam};
  const GridProblem problem = commonProblem();
  for (const SolverKind solver : SOLVERS)
  {
    SolverConfig config;
    config.solver = solver;
    config.timeoutMs = 0;
    config.randomIterations = 8;
    config.beamWidth = 8;
    config.maxExpandedStates = 5000;
    const GridSolution solution = solveGridProblem(problem, config);
    EXPECT_EQ(solution.status, SolveStatus::Solved) << toString(solver);
    EXPECT_TRUE(validateGridSolution(problem, solution).success) << toString(solver);
  }
}

TEST(GridSolver, UsesAllowedRotationWhenRequired)
{
  GridProblem problem;
  problem.problemId = "rotation-required";
  problem.sheet = {1, 2, "cell"};
  problem.parts = {{"domino", 1, {{0, 0}, {1, 0}}, {0, 90}}};

  const GridSolution solution = solveGridProblem(problem);
  ASSERT_EQ(solution.status, SolveStatus::Solved);
  ASSERT_EQ(solution.placements.size(), 1U);
  EXPECT_EQ(solution.placements.front().rotationDegrees, 90);
  EXPECT_TRUE(validateGridSolution(problem, solution).success);
}

TEST(GridSolver, EveryBaselineKeepsBestPartialWhenNoCompletePlacementIsFound)
{
  GridProblem problem;
  problem.problemId = "partial";
  problem.sheet = {2, 1, "cell"};
  problem.parts = {{"single", 3, {{0, 0}}, {0}}};

  constexpr std::array SOLVERS = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                  SolverKind::RandomLeftBottom, SolverKind::Beam};
  for (const SolverKind solver : SOLVERS)
  {
    SolverConfig config;
    config.solver = solver;
    config.timeoutMs = 0;
    config.randomIterations = 8;
    config.beamWidth = 8;
    config.maxExpandedStates = 5000;
    const GridSolution solution = solveGridProblem(problem, config);
    EXPECT_FALSE(solution.complete()) << toString(solver);
    EXPECT_EQ(solution.placements.size(), 2U) << toString(solver);
    EXPECT_EQ(solution.objective.placedParts, 2U) << toString(solver);
    EXPECT_TRUE(validateGridSolution(problem, solution).success) << toString(solver);
  }
}

TEST(GridSolver, AreaOrderingCanSolveTaskWhereInputOrderGetsStuck)
{
  GridProblem problem;
  problem.problemId = "order-dependent";
  problem.sheet = {2, 2, "cell"};
  problem.parts = {{"single", 1, {{0, 0}}, {0}}, {"L", 1, {{0, 0}, {0, 1}, {1, 1}}, {0}}};

  SolverConfig inputConfig;
  inputConfig.solver = SolverKind::InputFirstFit;
  inputConfig.timeoutMs = 0;
  const GridSolution inputResult = solveGridProblem(problem, inputConfig);
  EXPECT_EQ(inputResult.status, SolveStatus::NoSolutionFound);
  EXPECT_EQ(inputResult.placements.size(), 1U);

  SolverConfig areaConfig;
  areaConfig.solver = SolverKind::AreaLeftBottom;
  areaConfig.timeoutMs = 0;
  const GridSolution areaResult = solveGridProblem(problem, areaConfig);
  EXPECT_EQ(areaResult.status, SolveStatus::Solved);
  EXPECT_TRUE(validateGridSolution(problem, areaResult).success);
}

TEST(GridSolver, SeededRandomIsDeterministicApartFromTimings)
{
  SolverConfig config;
  config.solver = SolverKind::RandomLeftBottom;
  config.seed = 1234;
  config.randomIterations = 12;
  config.timeoutMs = 0;
  const GridSolution first = solveGridProblem(commonProblem(), config);
  const GridSolution second = solveGridProblem(commonProblem(), config);

  EXPECT_EQ(first.status, second.status);
  EXPECT_EQ(first.placements, second.placements);
  EXPECT_EQ(first.metrics.candidatesGenerated, second.metrics.candidatesGenerated);
  EXPECT_EQ(first.metrics.candidatesValidated, second.metrics.candidatesValidated);
  EXPECT_EQ(first.metrics.expandedStates, second.metrics.expandedStates);
}

TEST(GridSolver, BeamReportsDeterministicBudgetExhaustionWithPartial)
{
  SolverConfig config;
  config.solver = SolverKind::Beam;
  config.beamWidth = 4;
  config.maxExpandedStates = 1;
  config.timeoutMs = 0;
  const GridSolution solution = solveGridProblem(commonProblem(), config);
  EXPECT_EQ(solution.status, SolveStatus::BudgetExhausted);
  EXPECT_EQ(solution.placements.size(), 1U);
  EXPECT_EQ(solution.metrics.expandedStates, 1U);
  EXPECT_TRUE(validateGridSolution(commonProblem(), solution).success);
}

TEST(GridSolver, EmptyProblemIsTriviallySolved)
{
  GridProblem problem;
  problem.problemId = "empty";
  problem.sheet = {5, 3, "cell"};
  SolverConfig config;
  config.timeoutMs = 0;
  const GridSolution solution = solveGridProblem(problem, config);
  EXPECT_EQ(solution.status, SolveStatus::Solved);
  EXPECT_EQ(solution.objective.usedLength, 0);
  EXPECT_EQ(solution.objective.primaryRemnantWidth, 5);
}

TEST(GridSolver, EmergencyTimeoutReturnsTimedOutStatus)
{
  GridProblem problem;
  problem.problemId = "timeout";
  problem.sheet = {100, 100, "cell"};
  problem.parts = {{"single", 20, {{0, 0}}, {0}}};
  SolverConfig config;
  config.solver = SolverKind::Beam;
  config.beamWidth = 128;
  config.maxExpandedStates = 1000000;
  config.timeoutMs = 1;
  const GridSolution solution = solveGridProblem(problem, config);
  EXPECT_EQ(solution.status, SolveStatus::TimedOut);
}

TEST(GridSolver, ReportsProgressWithoutChangingAnyBaselineResult)
{
  constexpr std::array SOLVERS = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                  SolverKind::RandomLeftBottom, SolverKind::Beam};
  for (const SolverKind solver : SOLVERS)
  {
    SolverConfig config;
    config.solver = solver;
    config.timeoutMs = 0;
    config.randomIterations = 8;
    config.beamWidth = 8;
    config.maxExpandedStates = 5000;
    std::vector<SearchProgress> progress;
    SearchExecutionControl control;
    control.progress = [&progress](const SearchProgress & value)
    {
      progress.push_back(value);
    };

    const GridSolution reference = solveGridProblem(commonProblem(), config);
    const GridSolverExecutionResult controlled = runGridProblem(commonProblem(), config, control);

    ASSERT_FALSE(progress.empty()) << toString(solver);
    const SearchProgressStage expectedStage = solver == SolverKind::RandomLeftBottom ? SearchProgressStage::RandomIterations
                                            : solver == SolverKind::Beam             ? SearchProgressStage::ExpandedStates
                                                                                     : SearchProgressStage::Instances;
    EXPECT_TRUE(std::all_of(progress.begin(), progress.end(), [expectedStage](const SearchProgress & value)
                            { return value.stage == expectedStage && value.completed <= value.total; }));
    EXPECT_FALSE(controlled.cancelled);
    EXPECT_EQ(controlled.solution.status, reference.status);
    EXPECT_EQ(controlled.solution.placements, reference.placements);
    EXPECT_EQ(controlled.solution.objective.usedLength, reference.objective.usedLength);
    EXPECT_EQ(controlled.solution.metrics.candidatesGenerated, reference.metrics.candidatesGenerated);
    EXPECT_EQ(controlled.solution.metrics.candidatesValidated, reference.metrics.candidatesValidated);
    EXPECT_EQ(controlled.solution.metrics.expandedStates, reference.metrics.expandedStates);
  }
}

TEST(GridSolver, CancellationInsideCandidateLoopDoesNotMutateState)
{
  SolverConfig config;
  config.timeoutMs = 0;
  int polls = 0;
  SearchExecutionControl control;
  control.cancellationRequested = [&polls]()
  {
    return ++polls == 2;
  };

  const GridSolverExecutionResult result = runGridProblem(commonProblem(), config, control);

  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.solution.placements.empty());
  EXPECT_TRUE(validateGridSolution(commonProblem(), result.solution).success);
}

TEST(GridSolver, ReturnsBestPartialWhenCancelledAfterProgress)
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

  const GridSolverExecutionResult result = runGridProblem(commonProblem(), config, control);

  EXPECT_TRUE(result.cancelled);
  EXPECT_EQ(result.solution.placements.size(), 1);
  EXPECT_TRUE(validateGridSolution(commonProblem(), result.solution).success);
}

TEST(GridSolver, CancelsEveryBaselineBeforeFirstMutation)
{
  constexpr std::array SOLVERS = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                  SolverKind::RandomLeftBottom, SolverKind::Beam};
  for (const SolverKind solver : SOLVERS)
  {
    SolverConfig config;
    config.solver = solver;
    config.timeoutMs = 0;
    SearchExecutionControl control;
    control.cancellationRequested = []()
    {
      return true;
    };

    const GridSolverExecutionResult result = runGridProblem(commonProblem(), config, control);

    EXPECT_TRUE(result.cancelled) << toString(solver);
    EXPECT_TRUE(result.solution.placements.empty()) << toString(solver);
    EXPECT_TRUE(validateGridSolution(commonProblem(), result.solution).success) << toString(solver);
  }
}

TEST(GridSolver, PublicComparatorUsesTheSameLexicographicObjective)
{
  GridSolution reference;
  reference.status = SolveStatus::Solved;
  reference.objective = {.usedLength = 4, .largestExtraRectangleArea = 3, .fragmentationPenalty = 2};
  GridSolution better = reference;
  better.objective.usedLength = 3;
  EXPECT_TRUE(isBetterGridSolution(better, reference));
  EXPECT_FALSE(isBetterGridSolution(reference, better));

  better = reference;
  better.objective.largestExtraRectangleArea = 4;
  EXPECT_TRUE(isBetterGridSolution(better, reference));
}
