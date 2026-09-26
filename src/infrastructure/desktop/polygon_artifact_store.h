#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_ARTIFACT_STORE_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_ARTIFACT_STORE_H

#include <memory>
#include <optional>
#include <string>

#include <polygonworkspaceview.h>

namespace aipackaging::solver
{
class PolygonEnvironment;
struct PolygonProblem;
struct PolygonSolution;
} // namespace aipackaging::solver

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
namespace aipackaging::inference
{
class PolygonOnnxPolicy;
}
#endif

/// Потокобезопасно владеет задачами, проверенными решениями и моделями текущего процесса.
class PolygonArtifactStore
{
public:
  /// Представляет неизменяемую закрытую запись задачи и подготовленной геометрии.
  struct DocumentRecord;

  /// Создаёт пустое процессное хранилище.
  PolygonArtifactStore();
  /// Уничтожает закрытое содержимое после освобождения всех разделяемых записей.
  ~PolygonArtifactStore();

  /// Регистрирует проверенную задачу и возвращает новый процессный идентификатор.
  PolygonDocumentHandle addDocument(aipackaging::solver::PolygonProblem problem,
                                    std::shared_ptr<aipackaging::solver::PolygonEnvironment> environment);
  /// Возвращает зарегистрированную задачу либо пустой указатель.
  std::shared_ptr<const DocumentRecord> document(PolygonDocumentHandle handle) const;
  /// Проверяет и регистрирует решение соответствующей задачи.
  std::optional<PolygonSolutionHandle> addValidatedSolution(PolygonDocumentHandle document,
                                                            aipackaging::solver::PolygonSolution solution, std::string & error);
  /// Возвращает зарегистрированное проверенное решение либо пустой указатель.
  std::shared_ptr<const aipackaging::solver::PolygonSolution> solution(PolygonSolutionHandle handle) const;
  /// Без исключений удаляет задачу из хранилища.
  void release(PolygonDocumentHandle handle) noexcept;
  /// Без исключений удаляет решение из хранилища.
  void release(PolygonSolutionHandle handle) noexcept;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  /// Регистрирует проверенный комплект модели и возвращает процессный идентификатор.
  PolygonModelHandle addModel(std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model);
  /// Возвращает зарегистрированную модель либо пустой указатель.
  std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy> model(PolygonModelHandle handle) const;
  /// Без исключений удаляет модель из процессного хранилища.
  void release(PolygonModelHandle handle) noexcept;
#endif

private:
  /// Скрывает синхронизацию и контейнеры от потребителей инфраструктуры.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#endif
