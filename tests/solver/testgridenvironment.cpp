#include <limits>
#include <memory>
#include <string>

#include <aipackaging/nesting/grid_environment.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт минимальную валидную задачу с одной переданной деталью.
GridProblem problemWith(GridPart part, int columns = 6, int rows = 6)
{
  GridProblem problem;
  problem.problemId = "environment-test";
  problem.sheet = {columns, rows, "cell"};
  problem.parts.push_back(std::move(part));
  return problem;
}

/// Возвращает симметричную квадратную деталь для проверки дедупликации поворотов.
GridPart squarePart()
{
  return {"square", 1, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {0, 90, 180, 270}};
}
} // namespace

TEST(GridProblemValidation, AcceptsConnectedNormalizedPolyomino)
{
  EXPECT_TRUE(validateGridProblem(problemWith(squarePart())).success);
}

TEST(GridProblemValidation, RejectsDuplicateDisconnectedAndNonNormalizedCells)
{
  EXPECT_FALSE(validateGridProblem(problemWith({"duplicate", 1, {{0, 0}, {0, 0}}, {0}})).success);
  EXPECT_FALSE(validateGridProblem(problemWith({"disconnected", 1, {{0, 0}, {2, 0}}, {0}})).success);
  EXPECT_FALSE(validateGridProblem(problemWith({"shifted", 1, {{1, 1}}, {0}})).success);
}

TEST(GridProblemValidation, RejectsPartWithHole)
{
  GridPart ring{"ring", 1, {}, {0}};
  for (int row = 0; row < 3; ++row)
  {
    for (int column = 0; column < 3; ++column)
    {
      if (column != 1 || row != 1)
        ring.cells.push_back({column, row});
    }
  }
  EXPECT_FALSE(validateGridProblem(problemWith(std::move(ring))).success);
}

TEST(GridEnvironment, DeduplicatesSymmetricRotations)
{
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problemWith(squarePart()), error);
  ASSERT_NE(environment, nullptr) << error;
  ASSERT_EQ(environment->orientations(0).size(), 1U);
  EXPECT_EQ(environment->orientations(0).front().rotationDegrees, 0);
}

TEST(GridEnvironment, EnumeratesCandidatesInStableRotationColumnRowOrder)
{
  GridProblem problem = problemWith({"domino", 1, {{0, 0}, {1, 0}}, {90, 0}}, 3, 2);
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  ASSERT_NE(environment, nullptr) << error;

  const GridState state = environment->initialState();
  const std::vector<GridAction> candidates = environment->enumerateCandidates(state, 0);
  ASSERT_EQ(candidates.size(), 7U);
  EXPECT_EQ(candidates[0], (GridAction{"domino", 0, 0, 0, 0}));
  EXPECT_EQ(candidates[1], (GridAction{"domino", 0, 0, 1, 0}));
  EXPECT_EQ(candidates[4], (GridAction{"domino", 0, 0, 0, 90}));
  EXPECT_EQ(candidates[6], (GridAction{"domino", 0, 2, 0, 90}));
}

TEST(GridEnvironment, RejectsOverlapAndDuplicateInstance)
{
  GridProblem problem = problemWith({"single", 2, {{0, 0}}, {0}}, 2, 1);
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  ASSERT_NE(environment, nullptr) << error;
  GridState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"single", 0, 0, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"single", 0, 1, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"single", 1, 0, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"single", 1, -1, 0, 0}));
  EXPECT_FALSE(environment->canApply(state, {"single", 1, 2, 0, 0}));
  EXPECT_TRUE(environment->canApply(state, {"single", 1, 1, 0, 0}));
}

/// Проверяет, что крайние координаты недоверенного решения не переполняют проверку границ.
TEST(GridSolutionValidator, RejectsExtremeCoordinatesWithoutAccessingOccupancy)
{
  const GridProblem problem = problemWith({"single", 1, {{0, 0}}, {0}}, 2, 2);
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  ASSERT_NE(environment, nullptr) << error;
  const GridState empty = environment->initialState();
  for (const int coordinate : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
  {
    const GridAction horizontal{"single", 0, coordinate, 0, 0};
    const GridAction vertical{"single", 0, 0, coordinate, 0};
    EXPECT_FALSE(environment->canApply(empty, horizontal));
    EXPECT_FALSE(environment->canApply(empty, vertical));
    GridSolution solution;
    solution.problemId = problem.problemId;
    solution.status = SolveStatus::Solved;
    solution.placements = {horizontal};
    EXPECT_FALSE(validateGridSolution(problem, solution).success);
    solution.placements = {vertical};
    EXPECT_FALSE(validateGridSolution(problem, solution).success);
  }
  EXPECT_TRUE(environment->canApply(empty, {"single", 0, 1, 1, 0}));
  EXPECT_FALSE(environment->canApply(empty, {"single", 0, 2, 1, 0}));
}

TEST(GridEnvironment, ComputesValuableRemnantObjective)
{
  GridProblem problem = problemWith({"single", 4, {{0, 0}}, {0}}, 4, 3);
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  ASSERT_NE(environment, nullptr) << error;
  GridState state = environment->initialState();
  ASSERT_TRUE(environment->apply(state, {"single", 0, 1, 0, 0}));
  ASSERT_TRUE(environment->apply(state, {"single", 1, 1, 1, 0}));
  ASSERT_TRUE(environment->apply(state, {"single", 2, 1, 2, 0}));
  ASSERT_TRUE(environment->apply(state, {"single", 3, 2, 1, 0}));

  const ObjectiveComponents objective = environment->evaluate(state);
  EXPECT_EQ(objective.usedLength, 3);
  EXPECT_EQ(objective.primaryRemnantWidth, 1);
  EXPECT_EQ(objective.largestExtraRectangleArea, 3U);
  EXPECT_EQ(objective.fragmentationPenalty, 2U);
  EXPECT_EQ(objective.placedParts, 4U);
  EXPECT_EQ(objective.placedCells, 4U);
  EXPECT_DOUBLE_EQ(objective.materialUtilization, 4.0 / 12.0);
}

TEST(GridSolutionValidator, RejectsMismatchedObjective)
{
  GridProblem problem = problemWith({"single", 1, {{0, 0}}, {0}}, 2, 1);
  GridSolution solution;
  solution.problemId = problem.problemId;
  solution.status = SolveStatus::Solved;
  solution.placements.push_back({"single", 0, 0, 0, 0});
  solution.objective = {2, 0, 0, 0, 1, 1, 1, 1, 0.5};
  EXPECT_FALSE(validateGridSolution(problem, solution).success);
}
