#ifndef AIPACKAGING_SOLVER_POLYGONSOLVER_H
#define AIPACKAGING_SOLVER_POLYGONSOLVER_H

#include <cstdint>
#include <functional>

#include <polygontypes.h>

namespace aipackaging::solver
{
/// Этап выполнения полигонального baseline, определяющий смысл progress-счётчика.
enum class PolygonProgressStage : std::uint8_t
{
  Instances,
  RandomIterations,
  ExpandedStates
};

/// Наблюдаемый снимок прогресса без передачи изменяемого состояния решателя.
struct PolygonSolverProgress
{
  PolygonProgressStage stage = PolygonProgressStage::Instances;
  std::uint64_t completed = 0;
  std::uint64_t total = 0;
  std::size_t bestPlacedParts = 0;
  std::size_t totalParts = 0;
  std::uint64_t expandedStates = 0;
};

/// Необязательные функции управления длительным полигональным поиском.
struct PolygonExecutionControl
{
  std::function<bool()> cancellationRequested;
  std::function<void(const PolygonSolverProgress &)> progress;
};

/// Результат управляемого запуска с отдельным признаком пользовательской отмены.
struct PolygonSolverExecutionResult
{
  PolygonSolution solution;
  bool cancelled = false;
};

/// Сравнивает полигональные решения по общему full/partial objective-порядку M4.
bool isBetterPolygonSolution(const PolygonSolution & candidate, const PolygonSolution & reference);
/// Запускает baseline с поддержкой отмены, прогресса и возврата лучшего partial.
PolygonSolverExecutionResult runPolygonProblem(const PolygonProblem & problem, const SolverConfig & config,
                                               const PolygonExecutionControl & control = {});
/// Запускает выбранный baseline над полигональной средой.
PolygonSolution solvePolygonProblem(const PolygonProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
