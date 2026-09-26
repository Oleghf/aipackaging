#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_DOCUMENT_GATEWAY_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_DOCUMENT_GATEWAY_H

#include <memory>

#include <polygon_artifact_store.h>

/// Загружает и сохраняет полигональные JSON-документы через локальную файловую систему.
class LocalPolygonDocumentGateway final : public IPolygonDocumentGateway
{
public:
  /// Связывает шлюз с общим процессным хранилищем артефактов.
  explicit LocalPolygonDocumentGateway(std::shared_ptr<PolygonArtifactStore> store);
  /// Загружает, нормализует и регистрирует задачу.
  PolygonDocumentLoadResult load(const std::string & filePath) override;
  /// Сохраняет зарегистрированное проверенное решение.
  PolygonDocumentOperationResult save(const std::string & filePath, PolygonSolutionHandle solution) override;
  /// Без исключений освобождает зарегистрированную задачу.
  void release(PolygonDocumentHandle document) noexcept override;
  /// Без исключений освобождает зарегистрированное решение.
  void release(PolygonSolutionHandle solution) noexcept override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

#endif
