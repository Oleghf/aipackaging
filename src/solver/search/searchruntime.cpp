#include "searchruntime.h"

#include <algorithm>
#include <chrono>
#include <utility>

#ifndef AIPACKAGING_PROJECT_VERSION
#define AIPACKAGING_PROJECT_VERSION "unknown"
#endif

#ifndef AIPACKAGING_BUILD_REVISION
#define AIPACKAGING_BUILD_REVISION "unknown"
#endif

namespace aipackaging::solver::detail
{
namespace
{
/// Приводит длительность steady clock к целым микросекундам диагностических метрик.
std::uint64_t microseconds(SearchRuntime::Clock::duration value)
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(value).count());
}
} // namespace

/// Сохраняет ссылки на контракт запуска и фиксирует начало по внедрённым монотонным часам.
SearchRuntime::SearchRuntime(const SolverConfig & config, const SearchExecutionControl & control, ClockFunction clock)
  : config_(config)
  , control_(control)
  , clock_(std::move(clock))
  , started_(clock_())
{
}

/// Возвращает исходный объект конфигурации без копирования и изменения.
const SolverConfig & SearchRuntime::config() const
{
  return config_;
}

/// Делегирует получение времени внедрённому provider, чтобы тесты не зависели от wall clock.
SearchRuntime::Clock::time_point SearchRuntime::now() const
{
  return clock_();
}

/// Сначала фиксирует явную отмену пользователя, затем проверяет аварийный timeout.
bool SearchRuntime::pollStop()
{
  if (stopReason_ != SearchStopReason::None)
    return true;
  // Отмена имеет приоритет, если обе причины становятся наблюдаемыми на одной
  // безопасной границе поиска.
  if (control_.cancellationRequested && control_.cancellationRequested())
  {
    stopReason_ = SearchStopReason::Cancelled;
    return true;
  }
  if (config_.timeoutMs > 0 && now() - started_ >= std::chrono::milliseconds(config_.timeoutMs))
  {
    stopReason_ = SearchStopReason::TimedOut;
    return true;
  }
  return false;
}

/// Читает уже сохранённую причину, не опрашивая внешний callback повторно.
SearchStopReason SearchRuntime::stopReason() const
{
  return stopReason_;
}

/// Сопоставляет наблюдаемую причину остановки с публичным признаком cancellation.
bool SearchRuntime::cancellationObserved() const
{
  return stopReason_ == SearchStopReason::Cancelled;
}

/// Добавляет длительность успешного вызова генератора и размер возвращённого каталога.
void SearchRuntime::recordCandidateGeneration(Clock::time_point started, std::size_t generated)
{
  generationTime_ += now() - started;
  metrics_.candidatesGenerated += generated;
}

/// Добавляет длительность завершившейся проверки и увеличивает число проверенных кандидатов.
void SearchRuntime::recordValidation(Clock::time_point started)
{
  validationTime_ += now() - started;
  ++metrics_.candidatesValidated;
}

/// Увеличивает детерминированный счётчик фактически созданных состояний.
void SearchRuntime::recordExpansion()
{
  ++metrics_.expandedStates;
}

/// Читает детерминированный счётчик без завершения измерения времени.
std::uint64_t SearchRuntime::expandedStates() const
{
  return metrics_.expandedStates;
}

/// Собирает progress из общих счётчиков и вызывает callback непосредственно в потоке поиска.
void SearchRuntime::report(SearchProgressStage stage, std::uint64_t completed, std::uint64_t total, std::size_t bestPlacedParts,
                           std::size_t totalParts) const
{
  if (control_.progress)
    control_.progress({stage, completed, total, bestPlacedParts, totalParts, metrics_.expandedStates});
}

/// Вычитает явно измеренные генерацию и валидацию из общего времени, ограничивая округление нулём.
SolverMetrics SearchRuntime::finalizedMetrics() const
{
  SolverMetrics result = metrics_;
  result.candidateGenerationTimeUs = microseconds(generationTime_);
  result.validationTimeUs = microseconds(validationTime_);
  result.totalTimeUs = microseconds(now() - started_);
  const std::uint64_t measured = result.candidateGenerationTimeUs + result.validationTimeUs;
  result.searchTimeUs = result.totalTimeUs - std::min(result.totalTimeUs, measured);
  return result;
}

/// Копирует воспроизводимые параметры запуска и compile-time идентификаторы проекта в metadata.
SolverMetadata makeBaselineMetadata(const SolverConfig & config)
{
  SolverMetadata result;
  result.family = SolverFamily::Baseline;
  result.name = toString(config.solver);
  result.projectVersion = AIPACKAGING_PROJECT_VERSION;
  result.revision = AIPACKAGING_BUILD_REVISION;
  result.seed = config.seed;
  result.randomIterations = config.randomIterations;
  result.beamWidth = config.beamWidth;
  result.maxExpandedStates = config.maxExpandedStates;
  result.timeoutMs = config.timeoutMs;
  return result;
}
} // namespace aipackaging::solver::detail
