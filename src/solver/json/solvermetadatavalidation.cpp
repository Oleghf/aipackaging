#include "solvermetadatavalidation.h"

#include <algorithm>
#include <cctype>

#include <aipackaging/nesting/search_contracts.h>

namespace aipackaging::solver::internal
{
namespace
{
/// Проверяет SHA-256 с учётом требования полигональной схемы к нижнему регистру.
bool validSha256(const std::string & value, bool lowercaseOnly)
{
  if (value.size() != 64)
    return false;
  return std::all_of(value.begin(), value.end(),
                     [lowercaseOnly](unsigned char character)
                     {
                       if (std::isdigit(character) != 0 || (character >= 'a' && character <= 'f'))
                         return true;
                       return !lowercaseOnly && character >= 'A' && character <= 'F';
                     });
}
} // namespace

/// Сопоставляет разобранные значения с ограничениями клеточной и полигональной JSON Schema v1/v2.
bool validateSolverMetadata(const SolverMetadata & metadata, int wireVersion, SolverMetadataContract contract)
{
  const bool polygon = contract == SolverMetadataContract::Polygon;
  if (wireVersion == 1)
  {
    SolverKind kind;
    return metadata.family == SolverFamily::Baseline && parseSolverKind(metadata.name, kind) &&
           !metadata.projectVersion.empty() && !metadata.revision.empty() && metadata.randomIterations > 0 &&
           metadata.beamWidth > 0 && metadata.maxExpandedStates > 0;
  }
  if (wireVersion != 2 || metadata.name.empty() || metadata.projectVersion.empty() || metadata.revision.empty())
    return false;

  if (metadata.family != SolverFamily::Neural)
  {
    const bool positiveBudgets = metadata.randomIterations > 0 && metadata.beamWidth > 0 && metadata.maxExpandedStates > 0;
    if (!positiveBudgets)
      return false;
  }
  if (metadata.family == SolverFamily::Baseline)
  {
    SolverKind kind;
    return parseSolverKind(metadata.name, kind);
  }
  if (metadata.modelId.empty() || !validSha256(metadata.modelSha256, polygon) || metadata.rollouts == 0)
    return false;
  if (metadata.family == SolverFamily::Neural)
    return metadata.selectionMode == "greedy" || metadata.selectionMode == "sampled-best-of";
  return metadata.family == SolverFamily::Hybrid && metadata.selectionMode == "hybrid-best-of";
}
} // namespace aipackaging::solver::internal
