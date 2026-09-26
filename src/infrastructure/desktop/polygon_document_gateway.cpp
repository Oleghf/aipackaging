#include "polygon_document_gateway.h"

#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>

#include "polygon_artifact_store_internal.h"
#include "polygon_presentation_mapper.h"

using namespace aipackaging::solver;

/// Сохраняет общее хранилище, используемое шлюзом и внутренними реализациями.
LocalPolygonDocumentGateway::LocalPolygonDocumentGateway(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Сначала выполняет строгий разбор и нормализацию, затем публикует процессный документ.
PolygonDocumentLoadResult LocalPolygonDocumentGateway::load(const std::string & filePath)
{
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromFile(filePath);
  if (!loaded.success)
    return {false, loaded.error};
  std::string error;
  std::unique_ptr<PolygonEnvironment> created = PolygonEnvironment::Create(loaded.problem, error);
  if (!created)
    return {false, error};
  std::shared_ptr<PolygonEnvironment> environment(std::move(created));
  PolygonDocumentLoadResult result;
  result.success = true;
  result.problemId = loaded.problem.problemId;
  result.document = store_->addDocument(loaded.problem, environment);
  const auto record = store_->document(result.document);
  result.summary = aipackaging::desktop::detail::buildDocumentSummary(*record);
  aipackaging::desktop::detail::buildPresentation(*record, nullptr, result.scene, result.unplacedInstances);
  return result;
}

/// Находит только ранее проверенное решение и передаёт его строгому сериализатору.
PolygonDocumentOperationResult LocalPolygonDocumentGateway::save(const std::string & filePath,
                                                                 PolygonSolutionHandle solutionHandle)
{
  const auto value = store_->solution(solutionHandle);
  if (!value)
    return {false, "Проверенное решение больше недоступно"};
  std::string error;
  return {savePolygonSolutionToFile(filePath, *value, error), error};
}

/// Передаёт освобождение задачи общему хранилищу.
void LocalPolygonDocumentGateway::release(PolygonDocumentHandle document) noexcept
{
  store_->release(document);
}

/// Передаёт освобождение решения общему хранилищу.
void LocalPolygonDocumentGateway::release(PolygonSolutionHandle solution) noexcept
{
  store_->release(solution);
}
