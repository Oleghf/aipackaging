#ifndef AIPACKAGING_SOLVER_GRIDSOLVER_H
#define AIPACKAGING_SOLVER_GRIDSOLVER_H

#include <gridtypes.h>

namespace aipackaging::solver
{
/// Запускает выбранный baseline над общей средой и возвращает полный или лучший частичный результат.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
