#ifndef AIPACKAGING_NESTING_GRID_SOLVER_H
#define AIPACKAGING_NESTING_GRID_SOLVER_H

#include <aipackaging/nesting/grid_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Результат управляемого клеточного запуска с отдельным признаком пользовательской отмены.
struct GridSolverExecutionResult
{
  GridSolution solution;
  bool cancelled = false;
};

/// Сравнивает результаты по единому порядку полных и частичных решений M1.
bool isBetterGridSolution(const GridSolution & candidate, const GridSolution & reference);

/// Запускает клеточный базовый алгоритм с отменой и возвратом лучшего частичного решения.
GridSolverExecutionResult runGridProblem(const GridProblem & problem, const SolverConfig & config,
                                         const SearchExecutionControl & control = {});

/// Запускает выбранный базовый алгоритм и возвращает полное или лучшее частичное решение.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
