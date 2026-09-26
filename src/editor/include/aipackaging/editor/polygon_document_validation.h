#ifndef AIPACKAGING_EDITOR_POLYGON_DOCUMENT_VALIDATION_H
#define AIPACKAGING_EDITOR_POLYGON_DOCUMENT_VALIDATION_H

#include <cstdint>
#include <string>
#include <vector>

#include <aipackaging/editor/polygon_document.h>

namespace aipackaging::editor
{
/// Обозначает влияние диагностики на допустимость документа.
enum class DiagnosticSeverity : std::uint8_t
{
  Warning,
  Error
};

/// Обозначает устойчивую машинную причину проблемы редактируемого документа.
enum class DocumentDiagnosticCode : std::uint8_t
{
  InvalidEntityId,
  DuplicateEntityId,
  InvalidNextEntityId,
  EmptyProblemId,
  InvalidSheet,
  InvalidManufacturing,
  EmptyParts,
  EmptyPartId,
  DuplicatePartId,
  InvalidQuantity,
  InvalidRotations,
  MissingOuterPath,
  EmptyPath,
  OpenPath,
  InvalidPathTopology,
  InvalidCoordinate,
  KerfIgnored,
  UnsupportedObjective,
  ExactGeometryRejected
};

/// Связывает код и русское объяснение проблемы с конкретной сущностью.
struct DocumentDiagnostic
{
  DocumentDiagnosticCode code = DocumentDiagnosticCode::InvalidEntityId;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  EntityId entity;
  std::string message;
};

/// Возвращает все локальные структурные диагностики без изменения документа.
std::vector<DocumentDiagnostic> validateEditableDocument(const EditablePolygonDocument & document);
/// Сообщает, содержит ли список хотя бы одну блокирующую ошибку.
bool hasDocumentErrors(const std::vector<DocumentDiagnostic> & diagnostics) noexcept;
} // namespace aipackaging::editor

#endif
