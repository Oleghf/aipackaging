#ifndef AIPACKAGING_NESTING_GRID_SOLVER_H
#define AIPACKAGING_NESTING_GRID_SOLVER_H

#include <aipackaging/nesting/grid_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Результат управляемого grid-запуска с отдельным признаком пользовательской отмены.
struct GridSolverExecutionResult
{
  GridSolution solution;
  bool cancelled = false;
};

/// Сравнивает два результата по единому полному/partial objective-порядку M1.
bool isBetterGridSolution(const GridSolution & candidate, const GridSolution & reference);

/// Запускает grid baseline с поддержкой отмены, прогресса и возврата лучшего partial.
GridSolverExecutionResult runGridProblem(const GridProblem & problem, const SolverConfig & config,
                                         const SearchExecutionControl & control = {});

/// Запускает выбранный baseline над общей средой и возвращает полный или лучший частичный результат.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
