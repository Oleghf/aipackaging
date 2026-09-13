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

/// Проверяет, что независимый валидатор обнаруживает подмену целевой функции.
TEST(PolygonIo, RejectsMismatchedSolutionObjective)
{
  PolygonSolution solution = solvePolygonProblem(problem());
  ASSERT_TRUE(solution.complete());
  ++solution.objective.usedLength;
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
