#ifndef AIPACKAGING_EDITOR_POLYGON_EDITOR_INTERACTION_H
#define AIPACKAGING_EDITOR_POLYGON_EDITOR_INTERACTION_H

#include <optional>
#include <vector>

#include <aipackaging/editor/polygon_editor_commands.h>

namespace aipackaging::editor
{
/// Обозначает вид устойчивой сущности, найденной в редактируемом документе.
enum class EditorEntityKind : std::uint8_t
{
  Unknown,
  Part,
  Path,
  Segment,
  Point
};

/// Описывает сущность и её владельцев без передачи изменяемых указателей.
struct EditorEntityReference
{
  EntityId entity;
  EditorEntityKind kind = EditorEntityKind::Unknown;
  EntityId part;
  EntityId path;
  EntityId segment;
};

/// Находит вид сущности и её ближайших владельцев либо возвращает отсутствие.
std::optional<EditorEntityReference> findEditorEntity(const EditablePolygonDocument & document, EntityId entity);
/// Раскрывает детали, контуры и сегменты в устойчивый набор принадлежащих им точек.
std::vector<EntityId> collectMovablePointIds(const EditablePolygonDocument & document, const std::vector<EntityId> & selection);
/// Строит однозначное исправление диагностики либо сообщает, что автоматическое исправление недопустимо.
std::optional<EditorCommandBatch> suggestedDiagnosticFix(const EditablePolygonDocument & document,
                                                         const DocumentDiagnostic & diagnostic);
} // namespace aipackaging::editor

#endif
