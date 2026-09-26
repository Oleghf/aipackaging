#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_EDITABLE_DOCUMENT_GATEWAY_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_EDITABLE_DOCUMENT_GATEWAY_H

#include <memory>

#include <polygon_artifact_store.h>
#include <polygoneditablecontracts.h>

/// Загружает, компилирует и сохраняет редактируемые полигональные документы локально.
class LocalPolygonEditableDocumentGateway final : public IPolygonEditableDocumentGateway
{
public:
  /// Связывает редактируемые документы с общим хранилищем снимков решателя.
  explicit LocalPolygonEditableDocumentGateway(std::shared_ptr<PolygonArtifactStore> store);
  /// Загружает строгую задачу либо черновик по корневому формату JSON.
  PolygonEditableDocumentLoadResult load(const std::string & filePath) override;
  /// Загружает автоматический черновик и помечает состояние исходного файла.
  PolygonEditableDocumentLoadResult loadRecovery(const std::string & filePath) override;
  /// Проверяет и атомарно сохраняет неизменяемую задачу `polygon_problem` v1.
  PolygonDocumentOperationResult saveProblem(const std::string & filePath,
                                             const aipackaging::editor::EditablePolygonDocument & document) override;
  /// Атомарно сохраняет редактируемую модель в формате `polygon_draft` v1.
  PolygonDocumentOperationResult saveDraft(const std::string & filePath,
                                           const aipackaging::editor::EditablePolygonDocument & document,
                                           PolygonDocumentSource source, const std::string & sourceIdentifier,
                                           std::uint64_t generation,
                                           std::optional<PolygonSourceFingerprint> baseFingerprint) override;
  /// Возвращает размер и время изменения обычного файла без чтения его содержимого.
  std::optional<PolygonSourceFingerprint> sourceFingerprint(const std::string & filePath) override;
  /// Читает карточку восстановления и сравнивает отпечаток исходного файла.
  PolygonRecoveryCandidate inspectRecovery(const std::string & filePath) override;
  /// Удаляет только указанный обычный файл автоматического черновика.
  PolygonDocumentOperationResult removeRecovery(const std::string & filePath) override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

#endif
