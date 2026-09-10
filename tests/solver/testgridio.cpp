#include <string>

#include <gridenvironment.h>
#include <gridio.h>
#include <gridsolver.h>
#include <gtest/gtest.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт валидную задачу для round-trip проверок wire-формата.
GridProblem sampleProblem()
{
  GridProblem problem;
  problem.problemId = "round-trip";
  problem.sheet = {5, 4, "cell"};
  problem.parts = {{"L", 2, {{0, 0}, {0, 1}, {1, 1}}, {0, 90, 180, 270}}};
  return problem;
}
} // namespace

TEST(GridProblemIo, RoundTripsStrictVersionedJson)
{
  const GridProblemLoadResult loaded = loadGridProblemFromText(saveGridProblemToText(sampleProblem()));
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.problem.problemId, "round-trip");
  ASSERT_EQ(loaded.problem.parts.size(), 1U);
  EXPECT_EQ(loaded.problem.parts.front().quantity, 2U);
  EXPECT_EQ(loaded.problem.parts.front().allowedRotations.size(), 4U);
}

TEST(GridProblemIo, RejectsUnknownFieldAndUnsupportedVersion)
{
  std::string unknown = saveGridProblemToText(sampleProblem());
  unknown.replace(unknown.find("\"problemId\""), std::string("\"problemId\"").size(), "\"unknown\"");
  EXPECT_FALSE(loadGridProblemFromText(unknown).success);

  std::string version = saveGridProblemToText(sampleProblem());
  version.replace(version.find("\"version\": 1"), std::string("\"version\": 1").size(), "\"version\": 2");
  EXPECT_FALSE(loadGridProblemFromText(version).success);
}

TEST(GridProblemIo, RejectsMalformedJsonAndInvalidGeometry)
{
  EXPECT_FALSE(loadGridProblemFromText("{").success);
  GridProblem invalid = sampleProblem();
  invalid.parts.front().cells = {{0, 0}, {2, 0}};
  EXPECT_FALSE(loadGridProblemFromText(saveGridProblemToText(invalid)).success);
}

TEST(GridSolutionIo, RoundTripsSolverOutput)
{
  SolverConfig config;
  config.timeoutMs = 0;
  const GridProblem problem = sampleProblem();
  const GridSolution original = solveGridProblem(problem, config);
  const GridSolutionLoadResult loaded = loadGridSolutionFromText(saveGridSolutionToText(original));
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.solution.problemId, original.problemId);
  EXPECT_EQ(loaded.solution.status, original.status);
  EXPECT_EQ(loaded.solution.placements, original.placements);
  EXPECT_TRUE(validateGridSolution(problem, loaded.solution).success);
}

TEST(GridSolutionIo, RejectsUnknownSolverAndMismatchedMetrics)
{
  const GridProblem problem = sampleProblem();
  GridSolution solution = solveGridProblem(problem, SolverConfig{});
  std::string json = saveGridSolutionToText(solution);
  const std::size_t name = json.find("area-left-bottom");
  ASSERT_NE(name, std::string::npos);
  json.replace(name, std::string("area-left-bottom").size(), "unknown-solver");
  EXPECT_FALSE(loadGridSolutionFromText(json).success);

  ++solution.objective.usedLength;
  EXPECT_FALSE(validateGridSolution(problem, solution).success);
}

TEST(GridSolutionIo, RoundTripsStrictNeuralSolutionV2)
{
  GridSolution solution = solveGridProblem(sampleProblem(), SolverConfig{});
  solution.wireVersion = 2;
  solution.solver.family = SolverFamily::Neural;
  solution.solver.name = "grid-policy-v1";
  solution.solver.modelId = "fixture-policy";
  solution.solver.modelSha256 = std::string(64, 'b');
  solution.solver.rollouts = 16;
  solution.solver.selectionMode = "sampled-best-of";

  const GridSolutionLoadResult loaded = loadGridSolutionFromText(saveGridSolutionToText(solution));
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.solution.wireVersion, 2);
  EXPECT_EQ(loaded.solution.solver.family, SolverFamily::Neural);
  EXPECT_EQ(loaded.solution.solver.modelId, "fixture-policy");
  EXPECT_EQ(loaded.solution.solver.rollouts, 16U);
  EXPECT_TRUE(validateGridSolution(sampleProblem(), loaded.solution).success);
}

TEST(GridSolutionIo, RejectsInconsistentSolutionV2Provenance)
{
  GridSolution solution = solveGridProblem(sampleProblem(), SolverConfig{});
  solution.wireVersion = 2;
  solution.solver.family = SolverFamily::Neural;
  solution.solver.name = "grid-policy-v1";
  solution.solver.modelId = "fixture-policy";
  solution.solver.modelSha256 = std::string(64, 'c');
  solution.solver.rollouts = 1;
  solution.solver.selectionMode = "greedy";
  std::string json = saveGridSolutionToText(solution);
  const std::size_t hash = json.find(std::string(64, 'c'));
  ASSERT_NE(hash, std::string::npos);
  json.replace(hash, 64, "not-a-hash");
  EXPECT_FALSE(loadGridSolutionFromText(json).success);

  solution.solver.modelSha256 = std::string(64, 'c');
  solution.solver.selectionMode = "hybrid-best-of";
  EXPECT_FALSE(loadGridSolutionFromText(saveGridSolutionToText(solution)).success);
}
