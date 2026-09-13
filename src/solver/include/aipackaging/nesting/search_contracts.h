#ifndef AIPACKAGING_NESTING_SEARCH_CONTRACTS_H
#define AIPACKAGING_NESTING_SEARCH_CONTRACTS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace aipackaging::solver
{
/// Доступные реализации базовых решателей раскроя.
enum class SolverKind : std::uint8_t
{
  InputFirstFit,
  AreaLeftBottom,
  MaxSideLeftBottom,
  RandomLeftBottom,
  Beam
};

/// Воспроизводимые настройки выбранного решателя и его ограничений.
struct SolverConfig
{
  SolverKind solver = SolverKind::AreaLeftBottom;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50000;
  std::uint64_t timeoutMs = 30000;
};

/// Этап выполнения базового алгоритма, определяющий смысл счётчика хода работы.
enum class SearchProgressStage : std::uint8_t
{
  Instances,
  RandomIterations,
  ExpandedStates
};

/// Наблюдаемый снимок прогресса без передачи изменяемого состояния решателя.
struct SearchProgress
{
  SearchProgressStage stage = SearchProgressStage::Instances;
  std::uint64_t completed = 0;
  std::uint64_t total = 0;
  std::size_t bestPlacedParts = 0;
  std::size_t totalParts = 0;
  std::uint64_t expandedStates = 0;
};

/// Необязательные функции управления синхронным запуском базового алгоритма.
struct SearchExecutionControl
{
  /// Возвращает истину, когда вызывающий код просит остановиться до следующего безопасного изменения.
  std::function<bool()> cancellationRequested;
  /// Принимает ход выполнения в потоке решателя; исключения функции передаются вызывающему коду.
  std::function<void(const SearchProgress &)> progress;
};

/// Возвращает стабильное строковое имя алгоритма для форматов обмена и CLI.
std::string toString(SolverKind solver);
/// Преобразует публичное строковое имя в вид решателя.
bool parseSolverKind(const std::string & value, SolverKind & solver);
} // namespace aipackaging::solver

#endif
