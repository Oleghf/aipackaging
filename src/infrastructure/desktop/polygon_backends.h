#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_BACKENDS_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_BACKENDS_H

#include <memory>

#include <polygon_artifact_store.h>

/// Выполняет пять полигональных базовых алгоритмов и формирует проверенный результат.
class BaselinePolygonBackend final : public IPolygonNestingBackend
{
public:
  /// Связывает внутреннюю реализацию с процессным хранилищем задач и решений.
  explicit BaselinePolygonBackend(std::shared_ptr<PolygonArtifactStore> store);
  /// Запускает выбранный базовый алгоритм и повторно проверяет полученное решение.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Выполняет нейросетевой и гибридный раскрой над проверенным комплектом ONNX.
class OnnxPolygonBackend final : public IPolygonNestingBackend
{
public:
  /// Связывает внутреннюю реализацию с хранилищем задач, моделей и решений.
  explicit OnnxPolygonBackend(std::shared_ptr<PolygonArtifactStore> store);
  /// Выполняет выбранный нейросетевой режим и проверяет итоговое решение.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};
#endif

/// Направляет прикладной запрос в базовую или доступную нейросетевую реализацию.
class PolygonBackendRouter final : public IPolygonNestingBackend
{
public:
  /// Сохраняет обязательную базовую и необязательную нейросетевую реализации.
  PolygonBackendRouter(std::shared_ptr<IPolygonNestingBackend> baseline, std::shared_ptr<IPolygonNestingBackend> neural = {});
  /// Выбирает реализацию только по явно указанному методу запроса.
  NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) override;

private:
  std::shared_ptr<IPolygonNestingBackend> baseline_;
  std::shared_ptr<IPolygonNestingBackend> neural_;
};

#endif
