#ifndef AIPACKAGING_SEARCH_RUNTIME_H
#define AIPACKAGING_SEARCH_RUNTIME_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <aipackaging/nesting/common_contracts.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver::detail
{
/// Причина досрочной остановки одного запуска поиска.
enum class SearchStopReason : std::uint8_t
{
  None,
  Cancelled,
  TimedOut
};

/// Общий runtime времени, управления и диагностических счётчиков baseline-поиска.
class SearchRuntime
{
public:
  using Clock = std::chrono::steady_clock;
  using ClockFunction = std::function<Clock::time_point()>;

  /// Создаёт runtime для конфигурации и необязательных callback текущего синхронного запуска.
  SearchRuntime(
    const SolverConfig & config, const SearchExecutionControl & control, ClockFunction clock = [] { return Clock::now(); });

  /// Возвращает неизменяемую конфигурацию текущего запуска.
  const SolverConfig & config() const;
  /// Возвращает текущее монотонное время выбранного clock provider.
  Clock::time_point now() const;
  /// Проверяет отмену и timeout и сообщает, следует ли остановить поиск.
  bool pollStop();
  /// Возвращает уже зафиксированную причину остановки без повторного вызова callback.
  SearchStopReason stopReason() const;
  /// Сообщает, наблюдалась ли пользовательская отмена.
  bool cancellationObserved() const;
  /// Учитывает завершившуюся генерацию заданного количества кандидатов.
  void recordCandidateGeneration(Clock::time_point started, std::size_t generated);
  /// Учитывает завершившуюся точную проверку одного кандидата.
  void recordValidation(Clock::time_point started);
  /// Учитывает одно созданное дочернее или принятое последовательное состояние.
  void recordExpansion();
  /// Возвращает текущее число фактически созданных состояний.
  std::uint64_t expandedStates() const;
  /// Синхронно публикует компактный progress-снимок вызывающему коду.
  void report(SearchProgressStage stage, std::uint64_t completed, std::uint64_t total, std::size_t bestPlacedParts,
              std::size_t totalParts) const;
  /// Возвращает накопленные счётчики с рассчитанными временными компонентами.
  SolverMetrics finalizedMetrics() const;

private:
  const SolverConfig & config_;
  const SearchExecutionControl & control_;
  ClockFunction clock_;
  Clock::time_point started_;
  Clock::duration generationTime_{};
  Clock::duration validationTime_{};
  SolverMetrics metrics_;
  SearchStopReason stopReason_ = SearchStopReason::None;
};

/// Формирует единые baseline metadata из конфигурации и сведений сборки.
SolverMetadata makeBaselineMetadata(const SolverConfig & config);
} // namespace aipackaging::solver::detail

#endif
