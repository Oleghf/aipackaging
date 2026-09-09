#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <gridlearning.h>
#include <gridsolver.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт компактную задачу с поворотом и несколькими экземплярами для проверок среды.
GridProblem learningProblem()
{
  GridProblem problem;
  problem.problemId = "learning";
  problem.sheet = {3, 2, "cell"};
  problem.parts = {{"domino", 1, {{0, 0}, {1, 0}}, {0, 90}}, {"single", 2, {{0, 0}}, {0}}};
  return problem;
}

/// Создаёт обучаемую среду и завершает тест диагностикой при неожиданной ошибке.
std::unique_ptr<GridLearningEnvironment> createEnvironment(const GridProblem & problem, const GridLearningLimits & limits = {})
{
  std::string error;
  std::unique_ptr<GridLearningEnvironment> environment = GridLearningEnvironment::Create(problem, limits, error);
  EXPECT_NE(environment, nullptr) << error;
  return environment;
}
} // namespace

TEST(GridLearning, CatalogAndStaticObservationAreStableAcrossReset)
{
  std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(learningProblem());
  ASSERT_NE(environment, nullptr);
  const GridLearningObservation first = environment->reset();
  const GridLearningObservation second = environment->reset();

  EXPECT_EQ(environment->actionCount(), 19U);
  EXPECT_EQ(first.actionMask, second.actionMask);
  EXPECT_EQ(first.candidateFeatures, second.candidateFeatures);
  EXPECT_EQ(first.candidateInstance, second.candidateInstance);
  EXPECT_EQ(first.partMasks.size(), 3U * 4U * 2U * 2U);
  EXPECT_EQ(first.orientationMask.size(), 3U * 4U);
  EXPECT_EQ(first.partFeatures.size(), 3U * 7U);
  EXPECT_EQ(first.objective.size(), 7U);
  EXPECT_EQ(environment->action(0), (GridAction{"domino", 0, 0, 0, 0}));
  EXPECT_EQ(environment->action(5), (GridAction{"domino", 0, 1, 0, 90}));
}

TEST(GridLearning, StepUpdatesOnlyDynamicStateAndTerminatesOnCompletion)
{
  std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(learningProblem());
  ASSERT_NE(environment, nullptr);
  const GridLearningObservation initial = environment->reset();

  const std::size_t domino = environment->findAction({"domino", 0, 0, 0, 0});
  ASSERT_LT(domino, environment->actionCount());
  const GridLearningStepResult first = environment->step(domino);
  EXPECT_FALSE(first.terminated);
  EXPECT_EQ(first.observation.occupancy[0], 1U);
  EXPECT_EQ(first.observation.occupancy[1], 1U);
  EXPECT_EQ(first.observation.candidateFeatures, initial.candidateFeatures);
  EXPECT_FALSE(first.observation.remaining[0]);

  environment->step(environment->findAction({"single", 0, 2, 0, 0}));
  const GridLearningStepResult last = environment->step(environment->findAction({"single", 1, 2, 1, 0}));
  EXPECT_TRUE(last.terminated);
  EXPECT_TRUE(last.complete);
  EXPECT_FALSE(last.deadEnd);
  EXPECT_TRUE(environment->isTerminal());
  EXPECT_THROW(environment->step(0), std::runtime_error);
}

TEST(GridLearning, InvalidActionsDoNotChangeState)
{
  std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(learningProblem());
  ASSERT_NE(environment, nullptr);
  environment->reset();
  const std::size_t first = environment->findAction({"domino", 0, 0, 0, 0});
  environment->step(first);
  const std::uint64_t rank = environment->rank();
  const GridLearningObservation before = environment->observation();

  EXPECT_THROW(environment->step(environment->actionCount()), std::out_of_range);
  EXPECT_EQ(environment->rank(), rank);
  EXPECT_EQ(environment->observation().occupancy, before.occupancy);
  EXPECT_THROW(environment->step(first), std::invalid_argument);
  EXPECT_EQ(environment->rank(), rank);
  EXPECT_EQ(environment->observation().occupancy, before.occupancy);
}

TEST(GridLearning, DetectsIncompleteDeadEnd)
{
  GridProblem problem;
  problem.problemId = "dead-end";
  problem.sheet = {2, 2, "cell"};
  problem.parts = {{"single", 1, {{0, 0}}, {0}}, {"L", 1, {{0, 0}, {0, 1}, {1, 1}}, {0}}};
  std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(problem);
  ASSERT_NE(environment, nullptr);

  const GridLearningStepResult result = environment->step(environment->findAction({"single", 0, 0, 1, 0}));
  EXPECT_TRUE(result.terminated);
  EXPECT_FALSE(result.complete);
  EXPECT_TRUE(result.deadEnd);
  EXPECT_FALSE(result.observation.actionMask[0]);
}

TEST(GridLearning, RewardEqualsExactNormalizedMixedRankDelta)
{
  GridProblem problem;
  problem.problemId = "reward";
  problem.sheet = {2, 1, "cell"};
  problem.parts = {{"single", 1, {{0, 0}}, {0}}};
  std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(problem);
  ASSERT_NE(environment, nullptr);
  EXPECT_EQ(environment->rank(), 20U);
  EXPECT_EQ(environment->rankUpperBound(), 161U);

  const GridLearningStepResult result = environment->step(0);
  EXPECT_EQ(result.rewardComponents.rankBefore, 20U);
  EXPECT_EQ(result.rewardComponents.rankAfter, 119U);
  EXPECT_DOUBLE_EQ(result.reward, 99.0 / 161.0);
  EXPECT_EQ(result.rewardComponents.placedPartsDelta, 1);
  EXPECT_EQ(result.rewardComponents.placedCellsDelta, 1);
  EXPECT_EQ(result.rewardComponents.usedLengthDelta, 1);
}

TEST(GridLearning, ReplaysEveryBaselineByStableActionIndex)
{
  constexpr std::array SOLVERS = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                  SolverKind::RandomLeftBottom, SolverKind::Beam};
  const GridProblem problem = learningProblem();
  for (const SolverKind solver : SOLVERS)
  {
    SolverConfig config;
    config.solver = solver;
    config.timeoutMs = 0;
    config.randomIterations = 8;
    config.beamWidth = 8;
    config.maxExpandedStates = 5000;
    const GridSolution solution = solveGridProblem(problem, config);
    ASSERT_TRUE(validateGridSolution(problem, solution).success) << toString(solver);

    std::unique_ptr<GridLearningEnvironment> environment = createEnvironment(problem);
    ASSERT_NE(environment, nullptr);
    for (const GridPlacement & placement : solution.placements)
    {
      const std::size_t index = environment->findAction(placement);
      ASSERT_LT(index, environment->actionCount()) << toString(solver);
      environment->step(index);
    }
    EXPECT_TRUE(environment->isTerminal()) << toString(solver);
    EXPECT_EQ(environment->isComplete(), solution.complete()) << toString(solver);
  }
}

TEST(GridLearning, RejectsTasksOutsideEnvironmentV1Limits)
{
  GridProblem tooLarge;
  tooLarge.problemId = "too-large";
  tooLarge.sheet = {65, 64, "cell"};
  tooLarge.parts = {{"single", 1, {{0, 0}}, {0}}};
  std::string error;
  EXPECT_EQ(GridLearningEnvironment::Create(tooLarge, {}, error), nullptr);
  EXPECT_NE(error.find("area"), std::string::npos);

  GridProblem tooMany = learningProblem();
  tooMany.parts = {{"single", 7, {{0, 0}}, {0}}};
  EXPECT_EQ(GridLearningEnvironment::Create(tooMany, {}, error), nullptr);
  EXPECT_NE(error.find("instance count"), std::string::npos);

  GridLearningLimits limits;
  limits.maxActions = 2;
  EXPECT_EQ(GridLearningEnvironment::Create(learningProblem(), limits, error), nullptr);
  EXPECT_NE(error.find("action catalog"), std::string::npos);
}
