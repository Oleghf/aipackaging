#ifndef AIPACKAGING_NESTING_POLYGON_SOLVER_H
#define AIPACKAGING_NESTING_POLYGON_SOLVER_H

#include <aipackaging/nesting/polygon_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Совместимое имя общего этапа хода выполнения для прежнего полигонального API.
using PolygonProgressStage = SearchProgressStage;

/// Совместимое имя общего снимка хода выполнения для прежнего полигонального API.
using PolygonSolverProgress = SearchProgress;

/// Совместимое имя общего управления выполнением для прежнего полигонального API.
using PolygonExecutionControl = SearchExecutionControl;

/// Результат управляемого запуска с отдельным признаком пользовательской отмены.
struct PolygonSolverExecutionResult
{
  PolygonSolution solution;
  bool cancelled = false;
};

/// Сравнивает полигональные решения по общему порядку полных и частичных результатов M4.
bool isBetterPolygonSolution(const PolygonSolution & candidate, const PolygonSolution & reference);
/// Запускает базовый алгоритм с отменой, ходом выполнения и лучшим частичным решением.
PolygonSolverExecutionResult runPolygonProblem(const PolygonProblem & problem, const SolverConfig & config,
                                               const PolygonExecutionControl & control = {});
/// Запускает выбранный базовый алгоритм над полигональной средой.
PolygonSolution solvePolygonProblem(const PolygonProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
