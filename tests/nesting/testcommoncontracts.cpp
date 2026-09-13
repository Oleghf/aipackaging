#include <aipackaging/nesting/common_contracts.h>
#include <gtest/gtest.h>

namespace aipackaging::solver
{
/// Проверяет неизменные строковые значения и обратное преобразование общих перечислений.
TEST(NestingCoreContracts, ConvertsCommonEnumsWithoutSolverImplementation)
{
  SolveStatus status = SolveStatus::InvalidProblem;
  EXPECT_EQ(toString(SolveStatus::Solved), "solved");
  EXPECT_EQ(toString(SolveStatus::NoSolutionFound), "no_solution_found");
  EXPECT_EQ(toString(SolveStatus::BudgetExhausted), "budget_exhausted");
  EXPECT_EQ(toString(SolveStatus::TimedOut), "timed_out");
  EXPECT_EQ(toString(SolveStatus::UnsupportedEnvironment), "unsupported_environment");
  EXPECT_EQ(toString(SolveStatus::InvalidProblem), "invalid_problem");
  EXPECT_TRUE(parseSolveStatus("budget_exhausted", status));
  EXPECT_EQ(status, SolveStatus::BudgetExhausted);
  EXPECT_FALSE(parseSolveStatus("unknown", status));

  SolverFamily family = SolverFamily::Baseline;
  EXPECT_EQ(toString(SolverFamily::Baseline), "baseline");
  EXPECT_EQ(toString(SolverFamily::Neural), "neural");
  EXPECT_EQ(toString(SolverFamily::Hybrid), "hybrid");
  EXPECT_TRUE(parseSolverFamily("hybrid", family));
  EXPECT_EQ(family, SolverFamily::Hybrid);
  EXPECT_FALSE(parseSolverFamily("unknown", family));
}

/// Проверяет значения по умолчанию нейтральных контрактов и независимый результат валидации.
TEST(NestingCoreContracts, PreservesCommonDefaults)
{
  const ObjectiveDefinition objective;
  EXPECT_EQ(objective.type, "valuable_right_remnant");
  EXPECT_EQ(objective.version, 1);

  const SolverMetadata metadata;
  EXPECT_EQ(metadata.family, SolverFamily::Baseline);
  EXPECT_EQ(metadata.seed, 42U);
  EXPECT_EQ(metadata.randomIterations, 64U);
  EXPECT_EQ(metadata.beamWidth, 32U);
  EXPECT_EQ(metadata.maxExpandedStates, 50000U);
  EXPECT_EQ(metadata.timeoutMs, 30000U);

  const SolverMetrics metrics;
  EXPECT_EQ(metrics.candidatesGenerated, 0U);
  EXPECT_EQ(metrics.candidatesValidated, 0U);
  EXPECT_EQ(metrics.expandedStates, 0U);
  EXPECT_EQ(metrics.totalTimeUs, 0U);

  const ValidationResult validation{false, "ошибка"};
  EXPECT_FALSE(validation.success);
  EXPECT_EQ(validation.error, "ошибка");
}
} // namespace aipackaging::solver
