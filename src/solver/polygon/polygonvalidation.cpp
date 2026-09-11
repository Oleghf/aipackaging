#include <cmath>
#include <set>

#include "polygoninternal.h"


namespace aipackaging::solver
{
using namespace internal;

/// Проверяет исходные скаляры и ограничения, не выполняя полную нормализацию кривых.
ValidationResult validatePolygonProblem(const PolygonProblem & problem)
{
  if (problem.problemId.empty())
    return {false, "problemId must not be empty"};
  if (problem.sheet.unit != "mm" || !std::isfinite(problem.sheet.width) || !std::isfinite(problem.sheet.height) ||
      problem.sheet.width <= 0 || problem.sheet.height <= 0 || problem.sheet.width > 10000 || problem.sheet.height > 10000)
    return {false, "sheet must use mm and fit the M4 size limit"};
  const PolygonManufacturing & m = problem.manufacturing;
  if (!std::isfinite(m.sheetMargin) || !std::isfinite(m.partSpacing) || !std::isfinite(m.kerf) ||
      !std::isfinite(m.curveTolerance) || m.sheetMargin < 0 || m.partSpacing < 0 || m.kerf < 0 || m.sheetMargin > 10000 ||
      m.partSpacing > 10000 || m.kerf > 10000 || m.curveTolerance < 0.001 || m.curveTolerance > 0.05 ||
      2.0 * m.sheetMargin >= std::min(problem.sheet.width, problem.sheet.height))
    return {false, "invalid manufacturing parameters"};
  if (problem.parts.empty() || problem.objective.type != "valuable_right_remnant" || problem.objective.version != 1)
    return {false, "parts must not be empty and objective must be valuable_right_remnant v1"};
  std::set<std::string> ids;
  std::size_t instances = 0;
  for (const PolygonPart & part : problem.parts)
  {
    if (part.id.empty() || !ids.insert(part.id).second || part.quantity == 0 || part.allowedRotations.empty())
      return {false, "part ids, quantities and rotations must be valid and unique"};
    instances += part.quantity;
    std::set<int> rotations;
    for (int rotation : part.allowedRotations)
      if ((rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) || !rotations.insert(rotation).second)
        return {false, "allowedRotations must be a unique subset of 0/90/180/270"};
  }
  if (instances > MAX_INSTANCES)
    return {false, "polygon problem exceeds 100 instances"};
  return {true, {}};
}

/// Создаёт новую среду и повторно применяет placements, не доверяя solution-метрикам.
ValidationResult validatePolygonSolution(const PolygonProblem & problem, const PolygonSolution & solution)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
    return {false, error};
  if (solution.problemId != problem.problemId)
    return {false, "solution problemId does not match problem"};
  PolygonState state = environment->initialState();
  for (const PolygonPlacement & placement : solution.placements)
    if (!environment->apply(state, placement))
      return {false, "solution contains an invalid polygon placement"};
  const PolygonObjectiveComponents actual = environment->evaluate(state);
  const PolygonObjectiveComponents & recorded = solution.objective;
  if (actual.usedLength != recorded.usedLength || actual.primaryRemnantWidth != recorded.primaryRemnantWidth ||
      actual.largestExtraRectangleArea != recorded.largestExtraRectangleArea ||
      actual.fragmentationPenalty != recorded.fragmentationPenalty || actual.placedParts != recorded.placedParts ||
      actual.totalParts != recorded.totalParts || actual.placedArea != recorded.placedArea ||
      actual.totalPartArea != recorded.totalPartArea ||
      std::abs(actual.materialUtilization - recorded.materialUtilization) > 1e-12 ||
      actual.rasterColumns != recorded.rasterColumns || actual.rasterRows != recorded.rasterRows)
    return {false, "polygon solution objective does not match geometry"};
  if ((solution.status == SolveStatus::Solved) != (actual.placedParts == actual.totalParts))
    return {false, "polygon solution status does not match completeness"};
  return {true, {}};
}

} // namespace aipackaging::solver
