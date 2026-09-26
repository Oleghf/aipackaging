#ifndef AIPACKAGING_APPLICATION_POLYGONEDITABLECONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONEDITABLECONTRACTS_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <activepolygondocument.h>
#include <aipackaging/editor/polygon_document.h>
#include <aipackaging/editor/polygon_document_validation.h>
#include <polygondocumentcontracts.h>

/// Фиксирует размер и время изменения исходного файла без раскрытия файловой системы приложению.
struct PolygonSourceFingerprint
{
  std::uint64_t size = 0;
  std::int64_t modifiedUnixNs = 0;

  /// Сравнивает оба компонента отпечатка источника.
  bool operator==(const PolygonSourceFingerprint &) const = default;
};

/// Возвращает редактируемый документ и, при полной корректности, снимок для решателя.
struct PolygonEditableDocumentLoadResult
{
  bool success = false;
  std::string error;
  aipackaging::editor::EditablePolygonDocument document;
  std::vector<aipackaging::editor::DocumentDiagnostic> diagnostics;
  std::optional<PolygonDocumentLoadResult> compiled;
  std::string problemId;
  PolygonDocumentSummary summary;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
  PolygonDocumentSource source = PolygonDocumentSource::None;
  std::string sourceIdentifier;
  std::uint64_t generation = 0;
  std::optional<PolygonSourceFingerprint> baseFingerprint;
  bool sourceChanged = false;
};

/// Скрывает JSON, часы и файловую систему сценариев редактируемого документа.
class IPolygonEditableDocumentGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonEditableDocumentGateway() = default;
  /// Загружает задачу либо черновик и выполняет локальную и точную проверки.
  virtual PolygonEditableDocumentLoadResult load(const std::string & filePath) = 0;
  /// Загружает найденный автоматический черновик как восстановленный документ.
  virtual PolygonEditableDocumentLoadResult loadRecovery(const std::string & filePath) = 0;
  /// Сохраняет точную задачу только из полностью проверяемого документа.
  virtual PolygonDocumentOperationResult saveProblem(const std::string & filePath,
                                                     const aipackaging::editor::EditablePolygonDocument & document) = 0;
  /// Сохраняет черновик с происхождением и поколением автоматического сохранения.
  virtual PolygonDocumentOperationResult saveDraft(const std::string & filePath,
                                                   const aipackaging::editor::EditablePolygonDocument & document,
                                                   PolygonDocumentSource source, const std::string & sourceIdentifier,
                                                   std::uint64_t generation,
                                                   std::optional<PolygonSourceFingerprint> baseFingerprint) = 0;
  /// Возвращает текущий отпечаток обычного файла либо отсутствие для недоступного пути.
  virtual std::optional<PolygonSourceFingerprint> sourceFingerprint(const std::string & filePath) = 0;
  /// Проверяет наличие, читаемость и актуальность автоматического черновика.
  virtual PolygonRecoveryCandidate inspectRecovery(const std::string & filePath) = 0;
  /// Удаляет автоматический черновик либо сообщает об отказе файловой системы.
  virtual PolygonDocumentOperationResult removeRecovery(const std::string & filePath) = 0;
};

#endif
