#ifndef AIPACKAGING_SOLVER_POLYGONSOLVER_H
#define AIPACKAGING_SOLVER_POLYGONSOLVER_H

#include <polygontypes.h>

namespace aipackaging::solver
{
/// Сравнивает полигональные решения по общему full/partial objective-порядку M4.
bool isBetterPolygonSolution(const PolygonSolution & candidate, const PolygonSolution & reference);
/// Запускает выбранный baseline над полигональной средой.
PolygonSolution solvePolygonProblem(const PolygonProblem & problem, const SolverConfig & config = {});
} // namespace aipackaging::solver

#endif
