#ifndef AIPACKAGING_NESTING_GRID_SOLVER_H
#define AIPACKAGING_NESTING_GRID_SOLVER_H

#include <aipackaging/nesting/grid_types.h>
#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver
{
/// Сравнивает два результата по единому полному/partial objective-порядку M1.
bool isBetterGridSolution(const GridSolution & candidate, const GridSolution & reference);

/// Запускает выбранный baseline над общей средой и возвращает полный или лучший частичный результат.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
