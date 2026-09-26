#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_ARTIFACT_STORE_INTERNAL_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_ARTIFACT_STORE_INTERNAL_H

#include <aipackaging/nesting/polygon_environment.h>
#include <polygon_artifact_store.h>

/// Хранит авторитетную задачу и подготовленную геометрию только внутри инфраструктуры.
struct PolygonArtifactStore::DocumentRecord
{
  aipackaging::solver::PolygonProblem problem;
  std::shared_ptr<aipackaging::solver::PolygonEnvironment> environment;
};

#endif
