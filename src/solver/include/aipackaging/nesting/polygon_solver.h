#ifndef AIPACKAGING_NESTING_POLYGON_SOLVER_H
#define AIPACKAGING_NESTING_POLYGON_SOLVER_H

#include <aipackaging/nesting/polygon_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Совместимое имя общего этапа прогресса для прежнего polygon API.
using PolygonProgressStage = SearchProgressStage;

/// Совместимое имя общего снимка прогресса для прежнего polygon API.
using PolygonSolverProgress = SearchProgress;

/// Совместимое имя общего execution-control для прежнего polygon API.
using PolygonExecutionControl = SearchExecutionControl;

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
