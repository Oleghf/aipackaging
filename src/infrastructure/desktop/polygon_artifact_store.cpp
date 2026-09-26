#include <mutex>
#include <unordered_map>
#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
#include <aipackaging/inference/polygon_onnx.h>
#endif

#include "polygon_artifact_store_internal.h"

using namespace aipackaging::solver;

/// Содержит синхронизацию, счётчики и таблицы закрытого процессного хранилища.
struct PolygonArtifactStore::Impl
{
  std::mutex mutex;
  std::uint64_t nextDocument = 1;
  std::uint64_t nextSolution = 1;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  std::uint64_t nextModel = 1;
#endif
  std::unordered_map<std::uint64_t, std::shared_ptr<const DocumentRecord>> documents;
  std::unordered_map<std::uint64_t, std::shared_ptr<const PolygonSolution>> solutions;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  std::unordered_map<std::uint64_t, std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy>> models;
#endif
};

/// Создаёт закрытую реализацию пустого хранилища.
PolygonArtifactStore::PolygonArtifactStore()
  : impl_(std::make_unique<Impl>())
{
}

/// Уничтожает закрытые записи после завершения всех потребителей.
PolygonArtifactStore::~PolygonArtifactStore() = default;

/// Добавляет неизменяемую запись под новым монотонным идентификатором.
PolygonDocumentHandle PolygonArtifactStore::addDocument(PolygonProblem problem, std::shared_ptr<PolygonEnvironment> environment)
{
  std::lock_guard lock(impl_->mutex);
  const PolygonDocumentHandle handle{impl_->nextDocument++};
  impl_->documents.emplace(handle.value,
                           std::make_shared<const DocumentRecord>(DocumentRecord{std::move(problem), std::move(environment)}));
  return handle;
}

/// Ищет запись под блокировкой и возвращает разделяемое неизменяемое владение.
std::shared_ptr<const PolygonArtifactStore::DocumentRecord> PolygonArtifactStore::document(PolygonDocumentHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->documents.find(handle.value);
  return found == impl_->documents.end() ? nullptr : found->second;
}

/// Проверяет решение относительно исходной задачи до выдачи сохраняемого идентификатора.
std::optional<PolygonSolutionHandle> PolygonArtifactStore::addValidatedSolution(PolygonDocumentHandle documentHandle,
                                                                                PolygonSolution solution, std::string & error)
{
  const auto source = document(documentHandle);
  if (!source)
  {
    error = "Загруженная задача больше недоступна";
    return std::nullopt;
  }
  const ValidationResult validation = validatePolygonSolution(source->problem, solution);
  if (!validation.success)
  {
    error = validation.error;
    return std::nullopt;
  }
  std::lock_guard lock(impl_->mutex);
  const PolygonSolutionHandle handle{impl_->nextSolution++};
  impl_->solutions.emplace(handle.value, std::make_shared<const PolygonSolution>(std::move(solution)));
  return handle;
}

/// Ищет проверенное решение и сохраняет его живым после снятия блокировки.
std::shared_ptr<const PolygonSolution> PolygonArtifactStore::solution(PolygonSolutionHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->solutions.find(handle.value);
  return found == impl_->solutions.end() ? nullptr : found->second;
}

/// Удаляет процессную ссылку на задачу.
void PolygonArtifactStore::release(PolygonDocumentHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->documents.erase(handle.value);
}

/// Удаляет процессную ссылку на решение.
void PolygonArtifactStore::release(PolygonSolutionHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->solutions.erase(handle.value);
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Добавляет неизменяемый исполнитель модели под новым монотонным идентификатором.
PolygonModelHandle PolygonArtifactStore::addModel(std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model)
{
  std::lock_guard lock(impl_->mutex);
  const PolygonModelHandle handle{impl_->nextModel++};
  impl_->models.emplace(handle.value, std::move(model));
  return handle;
}

/// Ищет модель под блокировкой и возвращает разделяемое неизменяемое владение.
std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy> PolygonArtifactStore::model(PolygonModelHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->models.find(handle.value);
  return found == impl_->models.end() ? nullptr : found->second;
}

/// Удаляет процессную ссылку на комплект модели.
void PolygonArtifactStore::release(PolygonModelHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->models.erase(handle.value);
}
#endif
