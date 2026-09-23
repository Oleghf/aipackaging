#include <algorithm>
#include <limits>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
/// Возвращает знак ориентированной площади треугольника без переполнения типа `int64_t`.
Wide cross(const PolygonPoint64 & a, const PolygonPoint64 & b, const PolygonPoint64 & c)
{
  return (static_cast<Wide>(b.x) - static_cast<Wide>(a.x)) * (static_cast<Wide>(c.y) - static_cast<Wide>(a.y)) -
         (static_cast<Wide>(b.y) - static_cast<Wide>(a.y)) * (static_cast<Wide>(c.x) - static_cast<Wide>(a.x));
}

/// Проверяет принадлежность точки замкнутому отрезку.
bool onSegment(const PolygonPoint64 & point, const PolygonPoint64 & a, const PolygonPoint64 & b)
{
  return cross(a, b, point) == 0.0L && point.x >= std::min(a.x, b.x) && point.x <= std::max(a.x, b.x) &&
         point.y >= std::min(a.y, b.y) && point.y <= std::max(a.y, b.y);
}

/// Классифицирует пересечение двух отрезков, отдельно сохраняя касание.
int segmentIntersection(const PolygonPoint64 & a, const PolygonPoint64 & b, const PolygonPoint64 & c, const PolygonPoint64 & d)
{
  const Wide abC = cross(a, b, c);
  const Wide abD = cross(a, b, d);
  const Wide cdA = cross(c, d, a);
  const Wide cdB = cross(c, d, b);
  if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0)) && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0)))
    return 2;
  if ((abC == 0 && onSegment(c, a, b)) || (abD == 0 && onSegment(d, a, b)) || (cdA == 0 && onSegment(a, c, d)) ||
      (cdB == 0 && onSegment(b, c, d)))
    return 1;
  return 0;
}

/// Вычисляет квадрат расстояния от точки до отрезка.
Wide pointSegmentDistanceSquared(const PolygonPoint64 & point, const PolygonPoint64 & a, const PolygonPoint64 & b)
{
  const Wide dx = static_cast<Wide>(b.x) - static_cast<Wide>(a.x);
  const Wide dy = static_cast<Wide>(b.y) - static_cast<Wide>(a.y);
  const Wide lengthSquared = dx * dx + dy * dy;
  if (lengthSquared == 0)
  {
    const Wide px = static_cast<Wide>(point.x) - static_cast<Wide>(a.x);
    const Wide py = static_cast<Wide>(point.y) - static_cast<Wide>(a.y);
    return px * px + py * py;
  }
  const Wide projection = std::clamp(
    ((static_cast<Wide>(point.x) - static_cast<Wide>(a.x)) * dx + (static_cast<Wide>(point.y) - static_cast<Wide>(a.y)) * dy) /
      lengthSquared,
    0.0L, 1.0L);
  const Wide px = (static_cast<Wide>(point.x) - static_cast<Wide>(a.x)) - projection * dx;
  const Wide py = (static_cast<Wide>(point.y) - static_cast<Wide>(a.y)) - projection * dy;
  return px * px + py * py;
}

/// Находит минимальный квадрат расстояния между границами двух колец.
Wide ringDistanceSquared(const PolygonRing64 & lhs, const PolygonRing64 & rhs)
{
  Wide best = std::numeric_limits<Wide>::max();
  for (std::size_t i = 0; i < lhs.size(); ++i)
  {
    const PolygonPoint64 & a = lhs[i];
    const PolygonPoint64 & b = lhs[(i + 1) % lhs.size()];
    for (std::size_t j = 0; j < rhs.size(); ++j)
    {
      const PolygonPoint64 & c = rhs[j];
      const PolygonPoint64 & d = rhs[(j + 1) % rhs.size()];
      if (segmentIntersection(a, b, c, d) != 0)
        return 0.0L;
      best = std::min({best, pointSegmentDistanceSquared(a, c, d), pointSegmentDistanceSquared(b, c, d),
                       pointSegmentDistanceSquared(c, a, b), pointSegmentDistanceSquared(d, a, b)});
    }
  }
  return best;
}

/// Классифицирует точку относительно кольца: -1 снаружи, 0 на границе, 1 внутри.
int pointInRing(const PolygonPoint64 & point, const PolygonRing64 & ring)
{
  bool inside = false;
  for (std::size_t index = 0, previous = ring.size() - 1; index < ring.size(); previous = index++)
  {
    const PolygonPoint64 & a = ring[previous];
    const PolygonPoint64 & b = ring[index];
    if (onSegment(point, a, b))
      return 0;
    const bool crosses = (a.y > point.y) != (b.y > point.y);
    if (crosses)
    {
      const Wide intersectionX = (static_cast<Wide>(b.x) - static_cast<Wide>(a.x)) *
                                   (static_cast<Wide>(point.y) - static_cast<Wide>(a.y)) /
                                   (static_cast<Wide>(b.y) - static_cast<Wide>(a.y)) +
                                 static_cast<Wide>(a.x);
      if (static_cast<Wide>(point.x) < intersectionX)
        inside = !inside;
    }
  }
  return inside ? 1 : -1;
}

/// Переносит кольцо только тогда, когда каждое сложение представимо в `int64_t`.
bool translateRing(const PolygonRing64 & ring, std::int64_t x, std::int64_t y, PolygonRing64 & result)
{
  result = ring;
  for (PolygonPoint64 & point : result)
  {
    if ((x > 0 && point.x > std::numeric_limits<std::int64_t>::max() - x) ||
        (x < 0 && point.x < std::numeric_limits<std::int64_t>::min() - x) ||
        (y > 0 && point.y > std::numeric_limits<std::int64_t>::max() - y) ||
        (y < 0 && point.y < std::numeric_limits<std::int64_t>::min() - y))
    {
      result.clear();
      return false;
    }
    point.x += x;
    point.y += y;
  }
  return true;
}

} // namespace internal

using namespace internal;

/// Проверяет экземпляр, границы, положительное перекрытие и евклидов зазор внешнего заменяющего контура.
bool PolygonEnvironment::canApply(const PolygonState & state, const PolygonAction & action) const
{
  const std::size_t instancePosition = findInstance(action.partId, action.instanceIndex);
  if (instancePosition >= instances_.size() || state.placedInstances.size() != instances_.size() ||
      state.placedInstances[instancePosition])
    return false;
  const PolygonOrientation * orientation = findOrientation(instances_[instancePosition].partIndex, action.rotationDegrees);
  if (!orientation)
    return false;
  const std::int64_t maxX = sheetWidth_ - sheetMargin_ - orientation->width;
  const std::int64_t maxY = sheetHeight_ - sheetMargin_ - orientation->height;
  if (action.x < sheetMargin_ || action.y < sheetMargin_ || action.x > maxX || action.y > maxY)
    return false;
  PolygonRing64 moving;
  if (!translateRing(orientation->outer, action.x, action.y, moving))
    return false;
  for (const PolygonPoint64 & point : moving)
    if (point.x < sheetMargin_ || point.y < sheetMargin_ || point.x > sheetWidth_ - sheetMargin_ ||
        point.y > sheetHeight_ - sheetMargin_)
      return false;
  const Wide requiredSquared = static_cast<Wide>(partSpacing_) * static_cast<Wide>(partSpacing_);
  for (const PolygonPlacement & placement : state.placements)
  {
    const PolygonRing64 fixed = placedOuter(placement);
    if (interiorsOverlap(moving, fixed) || (partSpacing_ > 0 && ringDistanceSquared(moving, fixed) < requiredSquared))
      return false;
  }
  return true;
}

} // namespace aipackaging::solver
