#ifndef AIPACKAGING_NESTING_SEARCH_CONTRACTS_H
#define AIPACKAGING_NESTING_SEARCH_CONTRACTS_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace aipackaging::solver
{
/// Доступные реализации baseline-решателей раскроя.
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

/// Возвращает стабильное строковое имя алгоритма для wire-форматов и CLI.
std::string toString(SolverKind solver);
/// Преобразует публичное строковое имя в вид решателя.
bool parseSolverKind(const std::string & value, SolverKind & solver);
} // namespace aipackaging::solver

#endif
