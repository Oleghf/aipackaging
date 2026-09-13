#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "searchruntime.h"

namespace
{
using namespace std::chrono_literals;
using namespace aipackaging::solver;
using namespace aipackaging::solver::detail;
} // namespace

/// Проверяет ограничение времени общего механизма без реального ожидания.
TEST(SearchRuntime, UsesInjectedMonotonicClockForTimeout)
{
  SolverConfig config;
  config.timeoutMs = 10;
  SearchRuntime::Clock::time_point current{};
  SearchRuntime runtime(config, {}, [&current]() { return current; });

  current += 9ms;
  EXPECT_FALSE(runtime.pollStop());
  current += 1ms;
  EXPECT_TRUE(runtime.pollStop());
  EXPECT_EQ(runtime.stopReason(), SearchStopReason::TimedOut);
}

/// Проверяет приоритет явной отмены при одновременном достижении ограничения времени.
TEST(SearchRuntime, GivesCancellationPriorityAtSameSafeBoundary)
{
  SolverConfig config;
  config.timeoutMs = 1;
  SearchExecutionControl control;
  control.cancellationRequested = []()
  {
    return true;
  };
  SearchRuntime::Clock::time_point current{};
  SearchRuntime runtime(config, control, [&current]() { return current; });

  current += 1ms;
  EXPECT_TRUE(runtime.pollStop());
  EXPECT_TRUE(runtime.cancellationObserved());
  EXPECT_EQ(runtime.stopReason(), SearchStopReason::Cancelled);
}

/// Проверяет общие счётчики, временные компоненты и снимок хода выполнения.
TEST(SearchRuntime, AccumulatesMetricsAndReportsProgress)
{
  SolverConfig config;
  config.timeoutMs = 0;
  std::vector<SearchProgress> progress;
  SearchExecutionControl control;
  control.progress = [&progress](const SearchProgress & value)
  {
    progress.push_back(value);
  };
  SearchRuntime::Clock::time_point current{};
  SearchRuntime runtime(config, control, [&current]() { return current; });

  const auto generationStarted = runtime.now();
  current += 3us;
  runtime.recordCandidateGeneration(generationStarted, 7);
  const auto validationStarted = runtime.now();
  current += 2us;
  runtime.recordValidation(validationStarted);
  runtime.recordExpansion();
  runtime.report(SearchProgressStage::ExpandedStates, 1, 10, 1, 4);
  current += 5us;

  const SolverMetrics metrics = runtime.finalizedMetrics();
  EXPECT_EQ(metrics.candidatesGenerated, 7);
  EXPECT_EQ(metrics.candidatesValidated, 1);
  EXPECT_EQ(metrics.expandedStates, 1);
  EXPECT_EQ(metrics.candidateGenerationTimeUs, 3);
  EXPECT_EQ(metrics.validationTimeUs, 2);
  EXPECT_EQ(metrics.searchTimeUs, 5);
  EXPECT_EQ(metrics.totalTimeUs, 10);
  ASSERT_EQ(progress.size(), 1);
  EXPECT_EQ(progress.front().stage, SearchProgressStage::ExpandedStates);
  EXPECT_EQ(progress.front().completed, 1);
  EXPECT_EQ(progress.front().total, 10);
  EXPECT_EQ(progress.front().bestPlacedParts, 1);
  EXPECT_EQ(progress.front().totalParts, 4);
  EXPECT_EQ(progress.front().expandedStates, 1);
}
