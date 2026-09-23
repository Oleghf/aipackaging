#ifndef AIPACKAGING_JSON_SOLVER_METADATA_VALIDATION_H
#define AIPACKAGING_JSON_SOLVER_METADATA_VALIDATION_H

#include <cstdint>

#include <aipackaging/nesting/common_contracts.h>

namespace aipackaging::solver::internal
{
/// Обозначает схему, чьи ограничения применяются к метаданным решателя.
enum class SolverMetadataContract : std::uint8_t
{
  Grid,
  Polygon
};

/// Проверяет значения метаданных после строгого разбора полей JSON.
bool validateSolverMetadata(const SolverMetadata & metadata, int wireVersion, SolverMetadataContract contract);
} // namespace aipackaging::solver::internal

#endif
