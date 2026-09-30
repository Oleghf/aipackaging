#include <algorithm>
#include <unordered_set>

#include <aipackaging/editor/polygon_editor_interaction.h>

namespace aipackaging::editor
{
namespace
{
/// Передаёт все используемые служебные точки сегмента в устойчивом порядке.
template<typename Callback>
void visitSegmentPoints(const EditableSegment & segment, Callback callback)
{
  if (segment.kind == EditableSegmentKind::Arc)
    callback(segment.center);
  else if (segment.kind == EditableSegmentKind::CubicBezier)
  {
    callback(segment.control1);
    callback(segment.control2);
  }
}

/// Передаёт вершины и служебные точки цепочки в порядке хранения документа.
template<typename Callback>
void visitPathPoints(const EditablePath & path, Callback callback)
{
  for (const EditablePoint & point : path.vertices)
    callback(point);
  for (const EditableSegment & segment : path.segments)
    visitSegmentPoints(segment, callback);
}

/// Передаёт внешний контур и отверстия детали в порядке документа.
template<typename Callback>
void visitPartPaths(const EditablePart & part, Callback callback)
{
  if (part.outer)
    callback(*part.outer);
  for (const EditablePath & hole : part.holes)
    callback(hole);
}

/// Добавляет точку к результату только один раз.
void appendUnique(EntityId point, std::unordered_set<std::uint64_t> & seen, std::vector<EntityId> & result)
{
  if (point && seen.insert(point.value).second)
    result.push_back(point);
}
} // namespace

/// Последовательно обходит иерархию документа и возвращает первое точное совпадение.
std::optional<EditorEntityReference> findEditorEntity(const EditablePolygonDocument & document, EntityId entity)
{
  if (!entity)
    return std::nullopt;
  for (const EditablePart & part : document.parts)
  {
    if (part.id == entity)
      return EditorEntityReference{entity, EditorEntityKind::Part, part.id, {}, {}};
    std::optional<EditorEntityReference> result;
    visitPartPaths(part,
                   [&](const EditablePath & path)
                   {
                     if (result)
                       return;
                     if (path.id == entity)
                     {
                       result = EditorEntityReference{entity, EditorEntityKind::Path, part.id, path.id, {}};
                       return;
                     }
                     for (const EditablePoint & point : path.vertices)
                       if (point.id == entity)
                       {
                         result = EditorEntityReference{entity, EditorEntityKind::Point, part.id, path.id, {}};
                         return;
                       }
                     for (const EditableSegment & segment : path.segments)
                     {
                       if (segment.id == entity)
                       {
                         result = EditorEntityReference{entity, EditorEntityKind::Segment, part.id, path.id, segment.id};
                         return;
                       }
                       visitSegmentPoints(segment,
                                          [&](const EditablePoint & point)
                                          {
                                            if (!result && point.id == entity)
                                              result = EditorEntityReference{entity, EditorEntityKind::Point, part.id, path.id,
                                                                             segment.id};
                                          });
                     }
                   });
    if (result)
      return result;
  }
  return std::nullopt;
}

/// Раскрывает каждую выбранную сущность и дедуплицирует точки без изменения порядка первого появления.
std::vector<EntityId> collectMovablePointIds(const EditablePolygonDocument & document, const std::vector<EntityId> & selection)
{
  std::vector<EntityId> result;
  std::unordered_set<std::uint64_t> seen;
  for (EntityId selected : selection)
  {
    const auto reference = findEditorEntity(document, selected);
    if (!reference)
      continue;
    for (const EditablePart & part : document.parts)
    {
      if (part.id != reference->part)
        continue;
      visitPartPaths(part,
                     [&](const EditablePath & path)
                     {
                       if (reference->kind == EditorEntityKind::Part ||
                           (reference->kind == EditorEntityKind::Path && path.id == reference->path))
                       {
                         visitPathPoints(path, [&](const EditablePoint & point) { appendUnique(point.id, seen, result); });
                         return;
                       }
                       if (path.id != reference->path)
                         return;
                       if (reference->kind == EditorEntityKind::Point)
                       {
                         appendUnique(reference->entity, seen, result);
                         return;
                       }
                       if (reference->kind == EditorEntityKind::Segment)
                       {
                         for (std::size_t index = 0; index < path.segments.size(); ++index)
                         {
                           const EditableSegment & segment = path.segments[index];
                           if (segment.id != reference->segment)
                             continue;
                           if (index < path.vertices.size())
                             appendUnique(path.vertices[index].id, seen, result);
                           const std::size_t end = index + 1 < path.vertices.size() ? index + 1 : 0;
                           if (end < path.vertices.size())
                             appendUnique(path.vertices[end].id, seen, result);
                           visitSegmentPoints(segment,
                                              [&](const EditablePoint & point) { appendUnique(point.id, seen, result); });
                         }
                       }
                     });
      break;
    }
  }
  return result;
}

/// Предлагает только изменения, смысл которых полностью определяется кодом диагностики.
std::optional<EditorCommandBatch> suggestedDiagnosticFix(const EditablePolygonDocument & document,
                                                         const DocumentDiagnostic & diagnostic)
{
  const auto reference = findEditorEntity(document, diagnostic.entity);
  if (!reference)
    return std::nullopt;
  if (diagnostic.code == DocumentDiagnosticCode::OpenPath && reference->kind == EditorEntityKind::Path)
  {
    for (const EditablePart & part : document.parts)
    {
      std::optional<EditorCommandBatch> result;
      visitPartPaths(part,
                     [&](const EditablePath & path)
                     {
                       if (path.id == diagnostic.entity && !path.closed && path.vertices.size() >= 3 &&
                           path.segments.size() + 1 == path.vertices.size())
                         result = EditorCommandBatch{"Замыкание контура", {ClosePathCommand{path.id, {}}}, std::nullopt};
                     });
      if (result)
        return result;
    }
  }
  if (diagnostic.code == DocumentDiagnosticCode::DegenerateSegment && reference->kind == EditorEntityKind::Segment)
    return EditorCommandBatch{"Удаление вырожденного сегмента", {DeleteSegmentCommand{diagnostic.entity}}, std::nullopt};
  return std::nullopt;
}
} // namespace aipackaging::editor
