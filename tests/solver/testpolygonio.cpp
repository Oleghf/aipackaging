#include <limits>
#include <memory>
#include <string>

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

/// Проверяет строгое двустороннее преобразование JSON и отказ от неизвестного поля.
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

/// Проверяет явную диагностику неподдерживаемой унаследованной клеточной сцены.
TEST(PolygonIo, RejectsRetiredPackingScene)
{
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(
    R"({"format":"aipackaging.packing_scene","version":1,"board":{"columns":1,"rows":1,"unit":"cell"},"objects":[],"actions":[]})");
  EXPECT_FALSE(loaded.success);
  EXPECT_NE(loaded.error.find("aipackaging.packing_scene"), std::string::npos);
  EXPECT_NE(loaded.error.find("больше не поддерживается"), std::string::npos);
}

/// Проверяет, что независимый валидатор обнаруживает подмену целевой функции.
TEST(PolygonIo, RejectsMismatchedSolutionObjective)
{
  PolygonSolution solution = solvePolygonProblem(problem());
  ASSERT_TRUE(solution.complete());
  ++solution.objective.usedLength;
  EXPECT_FALSE(validatePolygonSolution(problem(), solution).success);
}

/// Проверяет запрет статуса некорректной задачи после успешной нормализации задачи.
TEST(PolygonIo, RejectsInvalidProblemStatusForValidProblem)
{
  PolygonProblem value = problem();
  value.sheet = {10.0, 10.0, "mm"};
  value.manufacturing.sheetMargin = 0.0;
  PolygonSolution solution = solvePolygonProblem(value);
  ASSERT_FALSE(solution.complete());
  solution.status = SolveStatus::InvalidProblem;
  EXPECT_FALSE(validatePolygonSolution(value, solution).success);
}

/// Проверяет соответствие метаданных базового решателя ограничениям JSON Schema v1.
TEST(PolygonIo, RejectsInvalidBaselineMetadata)
{
  const std::string valid = savePolygonSolutionToText(solvePolygonProblem(problem()));

  std::string unknown = valid;
  const std::size_t name = unknown.find("\"area-left-bottom\"");
  ASSERT_NE(name, std::string::npos);
  unknown.replace(name, std::string("\"area-left-bottom\"").size(), "\"not-a-solver\"");
  EXPECT_FALSE(loadPolygonSolutionFromText(unknown).success);

  std::string zeroBudget = valid;
  const std::size_t budget = zeroBudget.find("\"randomIterations\": 64");
  ASSERT_NE(budget, std::string::npos);
  zeroBudget.replace(budget, std::string("\"randomIterations\": 64").size(), "\"randomIterations\": 0");
  EXPECT_FALSE(loadPolygonSolutionFromText(zeroBudget).success);
}

/// Проверяет безопасный отказ валидатора на предельных координатах размещения.
TEST(PolygonIo, RejectsExtremePlacementCoordinates)
{
  PolygonSolution solution = solvePolygonProblem(problem());
  ASSERT_FALSE(solution.placements.empty());
  solution.placements.front().x = std::numeric_limits<std::int64_t>::max();
  EXPECT_FALSE(validatePolygonSolution(problem(), solution).success);
  solution.placements.front().x = std::numeric_limits<std::int64_t>::min();
  EXPECT_FALSE(validatePolygonSolution(problem(), solution).success);
}

/// Проверяет, что слишком большое целое поворота отклоняется без исключения синтаксического анализатора.
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

/// Проверяет чтение и запись происхождения нейросетевого полигонального решения v2.
TEST(PolygonIo, RoundTripsNeuralSolutionV2)
{
  PolygonSolution solution = solvePolygonProblem(problem());
  ASSERT_TRUE(solution.complete());
  solution.wireVersion = 2;
  solution.solver.family = SolverFamily::Neural;
  solution.solver.name = "polygon-policy-v1";
  solution.solver.modelId = "fixture-policy";
  solution.solver.modelSha256 = std::string(64, 'a');
  solution.solver.rollouts = 16;
  solution.solver.selectionMode = "sampled-best-of";
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromText(savePolygonSolutionToText(solution));
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.solution.wireVersion, 2);
  EXPECT_EQ(loaded.solution.solver.family, SolverFamily::Neural);
  EXPECT_EQ(loaded.solution.solver.modelId, "fixture-policy");
  EXPECT_EQ(loaded.solution.solver.rollouts, 16U);
}
