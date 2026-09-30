#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <QObject>
#include <QPainterPathStroker>
#include <unordered_set>

#include <polygoneditorcanvasinteraction.h>

using namespace aipackaging::editor;

namespace
{
/// Добавляет сегмент редактора к пути Qt в системе координат документа.
void appendSegment(QPainterPath & path, const EditablePoint & start, const EditablePoint & end, const EditableSegment & segment)
{
  if (segment.kind == EditableSegmentKind::CubicBezier)
  {
    path.cubicTo(segment.control1.x, segment.control1.y, segment.control2.x, segment.control2.y, end.x, end.y);
    return;
  }
  if (segment.kind == EditableSegmentKind::Arc)
  {
    const double radius = std::hypot(start.x - segment.center.x, start.y - segment.center.y);
    if (radius <= 0.0 || !std::isfinite(radius))
    {
      path.lineTo(end.x, end.y);
      return;
    }
    const double startAngle = std::atan2(start.y - segment.center.y, start.x - segment.center.x) * 180.0 / std::numbers::pi;
    const double endAngle = std::atan2(end.y - segment.center.y, end.x - segment.center.x) * 180.0 / std::numbers::pi;
    double sweep = endAngle - startAngle;
    if (segment.clockwise && sweep > 0.0)
      sweep -= 360.0;
    if (!segment.clockwise && sweep < 0.0)
      sweep += 360.0;
    path.arcTo(QRectF(segment.center.x - radius, segment.center.y - radius, radius * 2.0, radius * 2.0), startAngle, sweep);
    return;
  }
  path.lineTo(end.x, end.y);
}

/// Передаёт все цепочки детали в порядке документа.
template<typename Callback>
void visitPaths(const EditablePart & part, Callback callback)
{
  if (part.outer)
    callback(*part.outer);
  for (const EditablePath & hole : part.holes)
    callback(hole);
}

/// Передаёт вершины и управляющие точки с их видом привязки.
template<typename Callback>
void visitPoints(const EditablePath & path, Callback callback)
{
  for (const EditablePoint & point : path.vertices)
    callback(point, PolygonSnapKind::Endpoint);
  for (const EditableSegment & segment : path.segments)
    if (segment.kind == EditableSegmentKind::Arc)
      callback(segment.center, PolygonSnapKind::ArcCenter);
}

/// Возвращает квадрат расстояния двух точек без вычисления корня.
double distanceSquared(const QPointF & lhs, const QPointF & rhs)
{
  const double dx = lhs.x() - rhs.x();
  const double dy = lhs.y() - rhs.y();
  return dx * dx + dy * dy;
}

/// Добавляет сущность в рамочный выбор только один раз.
void appendUnique(EntityId id, std::unordered_set<std::uint64_t> & seen, std::vector<EntityId> & result)
{
  if (id && seen.insert(id.value).second)
    result.push_back(id);
}
} // namespace

/// Последовательно воспроизводит сегменты и замыкание исходной цепочки.
QPainterPath editablePainterPath(const EditablePath & source)
{
  if (source.vertices.empty())
    return {};
  QPainterPath path(QPointF(source.vertices.front().x, source.vertices.front().y));
  const std::size_t count = std::min(source.segments.size(), source.vertices.size());
  for (std::size_t index = 0; index < count; ++index)
  {
    const EditablePoint & end = source.vertices[index + 1 < source.vertices.size() ? index + 1 : 0];
    appendSegment(path, source.vertices[index], end, source.segments[index]);
  }
  if (source.closed)
    path.closeSubpath();
  return path;
}

/// Сначала проверяет точки, затем границы сегментов и только после них заполнение контура.
std::optional<PolygonCanvasEntityHit> findEditableEntity(const EditablePolygonDocument & document, const QPointF & point,
                                                         double toleranceMm, const std::vector<EntityId> & selected,
                                                         EntityId activePart)
{
  /// Сохраняет приоритет, расстояние и устойчивый порядок одного совпадения.
  struct Candidate
  {
    PolygonCanvasEntityHit hit;
    int priority = 0;
    std::size_t order = 0;
  };
  std::vector<Candidate> candidates;
  std::unordered_set<std::uint64_t> selectedValues;
  for (EntityId id : selected)
    selectedValues.insert(id.value);
  std::size_t order = 0;
  for (const EditablePart & part : document.parts)
  {
    const int inactivePenalty = activePart && part.id != activePart ? 10 : 0;
    visitPaths(part,
               [&](const EditablePath & path)
               {
                 for (const EditablePoint & vertex : path.vertices)
                 {
                   const double distance = std::sqrt(distanceSquared(point, {vertex.x, vertex.y}));
                   if (distance <= toleranceMm)
                     candidates.push_back({{vertex.id, EditorEntityKind::Point, part.id, path.id, distance},
                                           inactivePenalty + (selectedValues.contains(vertex.id.value) ? 0 : 1),
                                           order++});
                 }
                 for (const EditableSegment & segment : path.segments)
                 {
                   const auto addPoint = [&](const EditablePoint & value)
                   {
                     const double distance = std::sqrt(distanceSquared(point, {value.x, value.y}));
                     if (distance <= toleranceMm)
                       candidates.push_back({{value.id, EditorEntityKind::Point, part.id, path.id, distance},
                                             inactivePenalty + (selectedValues.contains(value.id.value) ? 0 : 1),
                                             order++});
                   };
                   if (segment.kind == EditableSegmentKind::Arc)
                     addPoint(segment.center);
                   else if (segment.kind == EditableSegmentKind::CubicBezier)
                   {
                     addPoint(segment.control1);
                     addPoint(segment.control2);
                   }
                 }

                 const QPainterPath geometry = editablePainterPath(path);
                 QPainterPathStroker stroker;
                 stroker.setWidth(toleranceMm * 2.0);
                 const QPainterPath stroke = stroker.createStroke(geometry);
                 if (stroke.contains(point))
                 {
                   for (std::size_t index = 0; index < path.segments.size(); ++index)
                   {
                     QPainterPath one(QPointF(path.vertices[index].x, path.vertices[index].y));
                     const EditablePoint & end = path.vertices[index + 1 < path.vertices.size() ? index + 1 : 0];
                     appendSegment(one, path.vertices[index], end, path.segments[index]);
                     if (stroker.createStroke(one).contains(point))
                     {
                       candidates.push_back({{path.segments[index].id, EditorEntityKind::Segment, part.id, path.id, 0.0},
                                             inactivePenalty + 2,
                                             order++});
                       break;
                     }
                   }
                 }
                 if (path.closed && geometry.contains(point))
                   candidates.push_back({{path.id, EditorEntityKind::Path, part.id, path.id, 0.0}, inactivePenalty + 3, order++});
               });
  }
  if (candidates.empty())
    return std::nullopt;
  const auto best = std::min_element(
    candidates.begin(), candidates.end(), [](const Candidate & lhs, const Candidate & rhs)
    { return std::tie(lhs.priority, lhs.hit.distance, lhs.order) < std::tie(rhs.priority, rhs.hit.distance, rhs.order); });
  return best->hit;
}

/// Выбирает точки, сегменты и контуры активной детали по правилу направления рамки.
std::vector<EntityId> findEditableEntities(const EditablePolygonDocument & document, const QRectF & rectangle, bool crossing,
                                           EntityId activePart)
{
  const QRectF area = rectangle.normalized();
  std::vector<EntityId> result;
  std::unordered_set<std::uint64_t> seen;
  for (const EditablePart & part : document.parts)
  {
    if (activePart && part.id != activePart)
      continue;
    visitPaths(part,
               [&](const EditablePath & path)
               {
                 for (const EditablePoint & point : path.vertices)
                   if (area.contains(QPointF(point.x, point.y)))
                     appendUnique(point.id, seen, result);
                 for (const EditableSegment & segment : path.segments)
                 {
                   const auto inspect = [&](const EditablePoint & point)
                   {
                     if (area.contains(QPointF(point.x, point.y)))
                       appendUnique(point.id, seen, result);
                   };
                   if (segment.kind == EditableSegmentKind::Arc)
                     inspect(segment.center);
                   else if (segment.kind == EditableSegmentKind::CubicBezier)
                   {
                     inspect(segment.control1);
                     inspect(segment.control2);
                   }
                 }
                 const QPainterPath geometry = editablePainterPath(path);
                 const bool included = crossing ? geometry.intersects(area) || area.contains(geometry.boundingRect())
                                                : area.contains(geometry.boundingRect());
                 if (included)
                   appendUnique(path.id, seen, result);
                 QPainterPathStroker stroker;
                 stroker.setWidth(1.0e-6);
                 for (std::size_t index = 0; index < path.segments.size() && index < path.vertices.size(); ++index)
                 {
                   QPainterPath segmentPath(QPointF(path.vertices[index].x, path.vertices[index].y));
                   const EditablePoint & end = path.vertices[index + 1 < path.vertices.size() ? index + 1 : 0];
                   appendSegment(segmentPath, path.vertices[index], end, path.segments[index]);
                   const QRectF bounds = stroker.createStroke(segmentPath).boundingRect();
                   const bool segmentIncluded = crossing ? bounds.intersects(area) : area.contains(bounds);
                   if (segmentIncluded)
                     appendUnique(path.segments[index].id, seen, result);
                 }
               });
  }
  return result;
}

/// Выбирает ближайшую допустимую цель с закреплённым приоритетом и устойчивым разрешением равенства.
PolygonCanvasSnapResult snapEditablePoint(const EditablePolygonDocument & document, const QPointF & point, double toleranceMm,
                                          const PolygonCanvasSnapSettings & settings, EntityId activePart,
                                          const std::vector<EntityId> & excluded, EntityId closingVertex)
{
  PolygonCanvasSnapResult best{point};
  double bestDistance = std::numeric_limits<double>::infinity();
  int bestPriority = std::numeric_limits<int>::max();
  std::unordered_set<std::uint64_t> excludedValues;
  for (EntityId id : excluded)
    excludedValues.insert(id.value);
  const auto consider = [&](const EditablePoint & candidate, PolygonSnapKind kind, int priority, QString label)
  {
    if (excludedValues.contains(candidate.id.value))
      return;
    const double distance = std::sqrt(distanceSquared(point, {candidate.x, candidate.y}));
    if (distance > toleranceMm)
      return;
    if (priority < bestPriority ||
        (priority == bestPriority &&
         (distance < bestDistance || (distance == bestDistance && candidate.id.value < best.entity.value))))
    {
      best = {{candidate.x, candidate.y}, kind, candidate.id, std::move(label)};
      bestPriority = priority;
      bestDistance = distance;
    }
  };
  if (settings.geometryEnabled)
  {
    for (const EditablePart & part : document.parts)
    {
      if (activePart && part.id != activePart)
        continue;
      visitPaths(part,
                 [&](const EditablePath & path)
                 {
                   visitPoints(path,
                               [&](const EditablePoint & candidate, PolygonSnapKind kind)
                               {
                                 const bool closing = closingVertex && candidate.id == closingVertex;
                                 consider(candidate, closing ? PolygonSnapKind::ClosingVertex : kind,
                                          closing                             ? 0
                                          : kind == PolygonSnapKind::Endpoint ? 1
                                                                              : 2,
                                          closing                             ? QObject::tr("Первая вершина")
                                          : kind == PolygonSnapKind::Endpoint ? QObject::tr("Конечная точка")
                                                                              : QObject::tr("Центр дуги"));
                               });
                 });
    }
  }
  if (settings.gridEnabled && settings.gridStepMm > 0.0 && std::isfinite(settings.gridStepMm))
  {
    const QPointF grid(std::round(point.x() / settings.gridStepMm) * settings.gridStepMm,
                       std::round(point.y() / settings.gridStepMm) * settings.gridStepMm);
    const double distance = std::sqrt(distanceSquared(point, grid));
    if (distance <= toleranceMm && (3 < bestPriority || (3 == bestPriority && distance < bestDistance)))
      best = {grid, PolygonSnapKind::Grid, {}, QObject::tr("Сетка")};
  }
  return best;
}

/// Обходит только реальные вершины и служебные точки поддерживаемых сегментов.
std::optional<QPointF> editablePointPosition(const EditablePolygonDocument & document, EntityId point)
{
  for (const EditablePart & part : document.parts)
  {
    std::optional<QPointF> result;
    visitPaths(part,
               [&](const EditablePath & path)
               {
                 if (result)
                   return;
                 for (const EditablePoint & candidate : path.vertices)
                   if (candidate.id == point)
                     result = QPointF(candidate.x, candidate.y);
                 for (const EditableSegment & segment : path.segments)
                 {
                   const auto inspect = [&](const EditablePoint & candidate)
                   {
                     if (!result && candidate.id == point)
                       result = QPointF(candidate.x, candidate.y);
                   };
                   if (segment.kind == EditableSegmentKind::Arc)
                     inspect(segment.center);
                   else if (segment.kind == EditableSegmentKind::CubicBezier)
                   {
                     inspect(segment.control1);
                     inspect(segment.control2);
                   }
                 }
               });
    if (result)
      return result;
  }
  return std::nullopt;
}
