#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "searchalgorithms.h"
#include "searchruntime.h"

namespace
{
using namespace std::chrono_literals;
using namespace aipackaging::solver;
using namespace aipackaging::solver::detail;

/// Хранит минимальное состояние для проверки общего лучевого механизма.
struct BeamTestState
{
  std::vector<unsigned char> placed{0, 0};
  int identity = 0;
};

/// Описывает одно имитационное действие лучевого поиска.
struct BeamTestAction
{
  std::size_t instance = 0;
  int identity = 0;
  bool applicable = true;
};

/// Предоставляет управляемые каталоги и границу бюджета для тестов `runBeam`.
class BeamTestAdapter
{
public:
  using State = BeamTestState;
  using Action = BeamTestAction;

  /// Выбирает проверку бюджета до либо после валидации действия.
  explicit BeamTestAdapter(bool budgetBeforeValidation, bool equivalent = false)
    : budgetBeforeValidation_(budgetBeforeValidation)
    , equivalent_(equivalent)
  {
  }

  /// Возвращает пустое двухэлементное состояние.
  State initialState() const { return {}; }
  /// Возвращает число имитационных экземпляров.
  std::size_t instanceCount() const { return 2; }
  /// Сообщает, размещён ли выбранный экземпляр.
  bool isPlaced(const State & state, std::size_t index) const { return state.placed[index] != 0; }
  /// Управляемо считает экземпляры взаимозаменяемыми.
  bool samePart(std::size_t, std::size_t) const { return equivalent_; }
  /// Возвращает число размещённых экземпляров.
  std::size_t placedCount(const State & state) const
  {
    return static_cast<std::size_t>(state.placed[0]) + static_cast<std::size_t>(state.placed[1]);
  }
  /// Сообщает о полном состоянии.
  bool complete(const State & state) const { return placedCount(state) == instanceCount(); }
  /// Возвращает два допустимых действия и учитывает запрос экземпляра.
  std::vector<Action> candidates(const State &, std::size_t instance) const
  {
    requestedInstances.push_back(instance);
    return {{instance, static_cast<int>(instance * 10U + 1U), true}, {instance, static_cast<int>(instance * 10U + 2U), true}};
  }
  /// Возвращает управляемую допустимость действия.
  bool valid(const State &, const Action & action) const { return action.applicable; }
  /// Применяет действие к независимой копии состояния.
  void apply(State & state, const Action & action) const
  {
    state.placed[action.instance] = 1;
    state.identity = action.identity;
  }
  /// Сравнивает состояния только по количеству размещённых экземпляров.
  bool better(const State & candidate, const State & reference) const { return placedCount(candidate) > placedCount(reference); }
  /// Возвращает выбранную предметную границу бюджета.
  bool budgetBeforeValidation() const { return budgetBeforeValidation_; }

  mutable std::vector<std::size_t> requestedInstances;

private:
  bool budgetBeforeValidation_ = false;
  bool equivalent_ = false;
};
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

/// Проверяет прежнее различие проверки бюджета до и после точной валидации.
TEST(SearchRuntime, PreservesBeamBudgetValidationBoundary)
{
  for (bool beforeValidation : {false, true})
  {
    SolverConfig config;
    config.maxExpandedStates = 1;
    config.beamWidth = 2;
    config.timeoutMs = 0;
    SearchRuntime runtime(config, {});
    BeamTestAdapter adapter(beforeValidation);
    SolveStatus status = SolveStatus::NoSolutionFound;

    const BeamTestState best = runBeam(runtime, adapter, status);

    EXPECT_EQ(status, SolveStatus::BudgetExhausted);
    EXPECT_EQ(adapter.placedCount(best), 1U);
    EXPECT_EQ(runtime.expandedStates(), 1U);
    EXPECT_EQ(runtime.finalizedMetrics().candidatesValidated, beforeValidation ? 1U : 2U);
  }
}

/// Проверяет устранение перестановочного дубля меньшим неразмещённым индексом.
TEST(SearchRuntime, DetectsEarlierEquivalentBeamInstance)
{
  const BeamTestAdapter adapter(false, true);
  const BeamTestState state = adapter.initialState();
  EXPECT_FALSE(hasEarlierEquivalentUnplaced(adapter, state, 0));
  EXPECT_TRUE(hasEarlierEquivalentUnplaced(adapter, state, 1));
}

/// Проверяет сохранение входного порядка при равном предметном качестве состояний.
TEST(SearchRuntime, StablyTruncatesEquivalentBeamStates)
{
  const BeamTestAdapter adapter(false);
  std::vector<BeamTestState> states{{{1, 0}, 3}, {{1, 0}, 1}, {{1, 0}, 2}};
  truncateBeam(adapter, states, 2);
  ASSERT_EQ(states.size(), 2U);
  EXPECT_EQ(states[0].identity, 3);
  EXPECT_EQ(states[1].identity, 1);
}
