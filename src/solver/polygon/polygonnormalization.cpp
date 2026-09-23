#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <tuple>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
/// Округляет миллиметры к авторитетной микронной координате.
bool toMicrons(double value, std::int64_t & result)
{
  if (!std::isfinite(value))
    return false;
  const Wide scaled = static_cast<Wide>(value) * MICRONS_PER_MM;
  // На MSVC тип `long double` имеет точность `double`: верхняя граница `int64`
  // при приведении округляется до 2^63 и не годится для проверки через `max()`.
  const Wide upper = std::ldexp(1.0L, 63);
  if (scaled < -upper || scaled >= upper)
    return false;
  const Wide rounded = std::round(scaled);
  if (rounded < -upper || rounded >= upper)
    return false;
  result = static_cast<std::int64_t>(rounded);
  return true;
}

/// Проверяет микронный габарит без знакового вычитания до нормализации кольца.
bool extentWithinLimit(std::int64_t minimum, std::int64_t maximum)
{
  return static_cast<std::uint64_t>(maximum) - static_cast<std::uint64_t>(minimum) <= static_cast<std::uint64_t>(MAX_SHEET_UM);
}

/// Находит общий габарит колец и переводит все точки в безопасную локальную систему координат.
bool localizeRings(PolygonRing64 & outer, std::vector<PolygonRing64> & holes)
{
  std::int64_t minX = std::numeric_limits<std::int64_t>::max();
  std::int64_t minY = std::numeric_limits<std::int64_t>::max();
  std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
  std::int64_t maxY = std::numeric_limits<std::int64_t>::min();
  const auto inspect = [&](const PolygonRing64 & ring)
  {
    for (const PolygonPoint64 & point : ring)
    {
      minX = std::min(minX, point.x);
      minY = std::min(minY, point.y);
      maxX = std::max(maxX, point.x);
      maxY = std::max(maxY, point.y);
    }
  };
  inspect(outer);
  for (const PolygonRing64 & hole : holes)
    inspect(hole);
  if (!extentWithinLimit(minX, maxX) || !extentWithinLimit(minY, maxY))
    return false;
  const auto shift = [minX, minY](PolygonRing64 & ring)
  {
    for (PolygonPoint64 & point : ring)
    {
      // После проверки общего габарита обе разности лежат в [0, 10^7].
      point.x -= minX;
      point.y -= minY;
    }
  };
  shift(outer);
  for (PolygonRing64 & hole : holes)
    shift(hole);
  return true;
}

/// Сравнивает исходные точки с микронным допуском замыкания.
bool sourceEqual(const PolygonPointMm & lhs, const PolygonPointMm & rhs)
{
  return std::abs(lhs.x - rhs.x) <= 0.0005 && std::abs(lhs.y - rhs.y) <= 0.0005;
}

/// Добавляет точку кривой после округления и расходует общий предел вершин детали.
bool appendPoint(PolygonRing64 & ring, const PolygonPointMm & point, std::size_t & remainingVertices)
{
  PolygonPoint64 normalized;
  if (!toMicrons(point.x, normalized.x) || !toMicrons(point.y, normalized.y))
    return false;
  if (ring.empty() || !(ring.back() == normalized))
  {
    if (remainingVertices == 0)
      return false;
    ring.push_back(normalized);
    --remainingVertices;
  }
  return true;
}

/// Возвращает расстояние контрольной точки до конечной хорды в миллиметрах.
double pointSegmentDistance(const PolygonPointMm & point, const PolygonPointMm & start, const PolygonPointMm & end)
{
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const double lengthSquared = dx * dx + dy * dy;
  if (!std::isfinite(lengthSquared) || lengthSquared == 0.0)
    return std::hypot(point.x - start.x, point.y - start.y);
  const double projection = std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) / lengthSquared, 0.0, 1.0);
  return std::hypot(point.x - (start.x + projection * dx), point.y - (start.y + projection * dy));
}

/// Делит кривую Bézier до гарантированной близости к конечной хорде и соблюдает предел вершин.
bool flattenBezier(const PolygonPointMm & p0, const PolygonPointMm & p1, const PolygonPointMm & p2, const PolygonPointMm & p3,
                   double tolerance, int depth, PolygonRing64 & result, std::size_t & remainingVertices)
{
  const bool flat = pointSegmentDistance(p1, p0, p3) <= tolerance && pointSegmentDistance(p2, p0, p3) <= tolerance;
  if (flat)
    return appendPoint(result, p3, remainingVertices);
  if (depth >= 20)
    return false;
  const PolygonPointMm p01{(p0.x + p1.x) / 2.0, (p0.y + p1.y) / 2.0};
  const PolygonPointMm p12{(p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0};
  const PolygonPointMm p23{(p2.x + p3.x) / 2.0, (p2.y + p3.y) / 2.0};
  const PolygonPointMm p012{(p01.x + p12.x) / 2.0, (p01.y + p12.y) / 2.0};
  const PolygonPointMm p123{(p12.x + p23.x) / 2.0, (p12.y + p23.y) / 2.0};
  const PolygonPointMm middle{(p012.x + p123.x) / 2.0, (p012.y + p123.y) / 2.0};
  return flattenBezier(p0, p01, p012, middle, tolerance, depth + 1, result, remainingVertices) &&
         flattenBezier(middle, p123, p23, p3, tolerance, depth + 1, result, remainingVertices);
}

/// Проверяет все аналитические точки пути до вычисления радиусов и рекурсивного разбиения.
bool validateSourcePath(const PolygonPath & path)
{
  std::int64_t minX = std::numeric_limits<std::int64_t>::max();
  std::int64_t minY = std::numeric_limits<std::int64_t>::max();
  std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
  std::int64_t maxY = std::numeric_limits<std::int64_t>::min();
  const auto inspect = [&](const PolygonPointMm & point)
  {
    std::int64_t x = 0;
    std::int64_t y = 0;
    if (!toMicrons(point.x, x) || !toMicrons(point.y, y))
      return false;
    minX = std::min(minX, x);
    minY = std::min(minY, y);
    maxX = std::max(maxX, x);
    maxY = std::max(maxY, y);
    return true;
  };
  if (!inspect(path.start))
    return false;
  for (const PolygonSegment & segment : path.segments)
  {
    if (!inspect(segment.end) || (segment.kind == PolygonSegmentKind::Arc && !inspect(segment.center)) ||
        (segment.kind == PolygonSegmentKind::CubicBezier && (!inspect(segment.control1) || !inspect(segment.control2))))
      return false;
  }
  return extentWithinLimit(minX, maxX) && extentWithinLimit(minY, maxY);
}

/// Аппроксимирует один исходный путь с заданной ошибкой и общим пределом вершин детали.
bool flattenPath(const PolygonPath & path, double tolerance, PolygonRing64 & result, std::size_t & remainingVertices,
                 std::string & error)
{
  if (path.segments.empty())
  {
    error = "polygon path is empty";
    return false;
  }
  if (!validateSourcePath(path))
  {
    error = "polygon path extent or coordinates are outside normalized M4 limits";
    return false;
  }
  if (!appendPoint(result, path.start, remainingVertices))
  {
    error = "polygon path exceeds the supported vertex budget";
    return false;
  }
  PolygonPointMm current = path.start;
  for (const PolygonSegment & segment : path.segments)
  {
    if (segment.kind == PolygonSegmentKind::Line)
    {
      if (!appendPoint(result, segment.end, remainingVertices))
      {
        error = "line contains invalid coordinates";
        return false;
      }
    }
    else if (segment.kind == PolygonSegmentKind::Arc)
    {
      const double startAngle = std::atan2(current.y - segment.center.y, current.x - segment.center.x);
      const double endAngle = std::atan2(segment.end.y - segment.center.y, segment.end.x - segment.center.x);
      const double radius = std::hypot(current.x - segment.center.x, current.y - segment.center.y);
      const double endRadius = std::hypot(segment.end.x - segment.center.x, segment.end.y - segment.center.y);
      if (!std::isfinite(radius) || radius <= tolerance || std::abs(radius - endRadius) > 0.001)
      {
        error = "arc endpoints must have the same non-zero radius";
        return false;
      }
      double sweep = endAngle - startAngle;
      if (segment.clockwise)
      {
        while (sweep >= 0.0)
          sweep -= 2.0 * std::numbers::pi;
      }
      else
      {
        while (sweep <= 0.0)
          sweep += 2.0 * std::numbers::pi;
      }
      const double maxAngle = 2.0 * std::acos(std::clamp(1.0 - tolerance / radius, -1.0, 1.0));
      const double requiredPieces = std::ceil(std::abs(sweep) / maxAngle);
      if (!std::isfinite(startAngle) || !std::isfinite(endAngle) || !std::isfinite(sweep) || !std::isfinite(maxAngle) ||
          maxAngle <= 0.0 || !std::isfinite(requiredPieces) || requiredPieces < 1.0 ||
          requiredPieces > static_cast<double>(remainingVertices))
      {
        error = "arc approximation exceeds the supported vertex budget";
        return false;
      }
      const std::size_t pieces = static_cast<std::size_t>(requiredPieces);
      for (std::size_t index = 1; index <= pieces; ++index)
      {
        const double angle = startAngle + sweep * static_cast<double>(index) / static_cast<double>(pieces);
        const PolygonPointMm point{segment.center.x + radius * std::cos(angle), segment.center.y + radius * std::sin(angle)};
        if (!appendPoint(result, index == pieces ? segment.end : point, remainingVertices))
        {
          error = "arc approximation is outside coordinate range";
          return false;
        }
      }
    }
    else
    {
      if (!flattenBezier(current, segment.control1, segment.control2, segment.end, tolerance, 0, result, remainingVertices))
      {
        error = "Bezier approximation is outside coordinate range";
        return false;
      }
    }
    current = segment.end;
  }
  if (!sourceEqual(current, path.start))
  {
    error = "polygon path is not closed";
    return false;
  }
  if (result.size() > 1 && result.front() == result.back())
  {
    result.pop_back();
    ++remainingVertices;
  }

  // Очистка коллинеарных точек вызывает векторное произведение. Сначала
  // отклоняем кольца, чьи разности координат не представимы безопасно.
  std::int64_t minX = std::numeric_limits<std::int64_t>::max();
  std::int64_t minY = std::numeric_limits<std::int64_t>::max();
  std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
  std::int64_t maxY = std::numeric_limits<std::int64_t>::min();
  for (const PolygonPoint64 & point : result)
  {
    minX = std::min(minX, point.x);
    minY = std::min(minY, point.y);
    maxX = std::max(maxX, point.x);
    maxY = std::max(maxY, point.y);
  }
  if (!extentWithinLimit(minX, maxX) || !extentWithinLimit(minY, maxY))
  {
    error = "polygon part extent is outside normalized M4 limits";
    return false;
  }

  // Удалять можно только точку между соседями. Коллинеарный обратный ход
  // является частью геометрии и не должен исчезать как обычная избыточная вершина.
  bool changed = true;
  while (changed && result.size() >= 3)
  {
    changed = false;
    for (std::size_t index = 0; index < result.size(); ++index)
    {
      const PolygonPoint64 & previous = result[(index + result.size() - 1) % result.size()];
      const PolygonPoint64 & current = result[index];
      const PolygonPoint64 & next = result[(index + 1) % result.size()];
      if (cross(previous, current, next) == 0 && current.x >= std::min(previous.x, next.x) &&
          current.x <= std::max(previous.x, next.x) && current.y >= std::min(previous.y, next.y) &&
          current.y <= std::max(previous.y, next.y))
      {
        result.erase(result.begin() + static_cast<std::ptrdiff_t>(index));
        changed = true;
        break;
      }
    }
  }
  if (result.size() < 3 || result.size() > MAX_VERTICES)
  {
    error = "polygon ring has unsupported vertex count after approximation";
    return false;
  }
  return true;
}

/// Возвращает удвоенную ориентированную площадь кольца.
Wide signedDoubleArea(const PolygonRing64 & ring)
{
  Wide area = 0;
  for (std::size_t index = 0; index < ring.size(); ++index)
  {
    const PolygonPoint64 & a = ring[index];
    const PolygonPoint64 & b = ring[(index + 1) % ring.size()];
    area += static_cast<Wide>(a.x) * static_cast<Wide>(b.y) - static_cast<Wide>(b.x) * static_cast<Wide>(a.y);
  }
  return area;
}

/// Проверяет отсутствие самопересечений несоседних рёбер.
bool simpleRing(const PolygonRing64 & ring)
{
  for (std::size_t i = 0; i < ring.size(); ++i)
  {
    for (std::size_t j = i + 1; j < ring.size(); ++j)
    {
      if (j == i + 1 || (i == 0 && j + 1 == ring.size()))
        continue;
      if (segmentIntersection(ring[i], ring[(i + 1) % ring.size()], ring[j], ring[(j + 1) % ring.size()]) != 0)
        return false;
    }
  }
  return true;
}

/// Проверяет, что два кольца не касаются и не пересекаются.
bool ringsDisjoint(const PolygonRing64 & lhs, const PolygonRing64 & rhs)
{
  for (std::size_t i = 0; i < lhs.size(); ++i)
    for (std::size_t j = 0; j < rhs.size(); ++j)
      if (segmentIntersection(lhs[i], lhs[(i + 1) % lhs.size()], rhs[j], rhs[(j + 1) % rhs.size()]) != 0)
        return false;
  return true;
}

/// Поворачивает микронную точку на разрешённый четверть-оборот.
PolygonPoint64 rotatePoint(const PolygonPoint64 & point, int degrees)
{
  switch (degrees)
  {
    case 90:
      return {-point.y, point.x};
    case 180:
      return {-point.x, -point.y};
    case 270:
      return {point.y, -point.x};
    default:
      return point;
  }
}

/// Поворачивает геометрию и переносит минимум внешнего кольца в начало координат.
PolygonOrientation makeOrientation(const PolygonRing64 & outer, const std::vector<PolygonRing64> & holes, int degrees)
{
  PolygonOrientation result;
  result.rotationDegrees = degrees;
  for (const PolygonPoint64 & point : outer)
    result.outer.push_back(rotatePoint(point, degrees));
  for (const PolygonRing64 & hole : holes)
  {
    PolygonRing64 rotated;
    for (const PolygonPoint64 & point : hole)
      rotated.push_back(rotatePoint(point, degrees));
    result.holes.push_back(std::move(rotated));
  }
  const auto minX =
    std::min_element(result.outer.begin(), result.outer.end(), [](const auto & a, const auto & b) { return a.x < b.x; })->x;
  const auto minY =
    std::min_element(result.outer.begin(), result.outer.end(), [](const auto & a, const auto & b) { return a.y < b.y; })->y;
  auto shift = [minX, minY](PolygonRing64 & ring)
  {
    for (PolygonPoint64 & point : ring)
    {
      point.x -= minX;
      point.y -= minY;
    }
    const auto first = std::min_element(ring.begin(), ring.end(), [](const PolygonPoint64 & lhs, const PolygonPoint64 & rhs)
                                        { return std::tie(lhs.x, lhs.y) < std::tie(rhs.x, rhs.y); });
    std::rotate(ring.begin(), first, ring.end());
  };
  shift(result.outer);
  for (PolygonRing64 & hole : result.holes)
    shift(hole);
  std::sort(result.holes.begin(), result.holes.end(),
            [](const PolygonRing64 & lhs, const PolygonRing64 & rhs)
            {
              return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  [](const PolygonPoint64 & a, const PolygonPoint64 & b)
                                                  { return std::tie(a.x, a.y) < std::tie(b.x, b.y); });
            });
  result.width =
    std::max_element(result.outer.begin(), result.outer.end(), [](const auto & a, const auto & b) { return a.x < b.x; })->x;
  result.height =
    std::max_element(result.outer.begin(), result.outer.end(), [](const auto & a, const auto & b) { return a.y < b.y; })->y;
  Wide area = std::abs(signedDoubleArea(result.outer));
  for (const PolygonRing64 & hole : result.holes)
    area -= std::abs(signedDoubleArea(hole));
  result.materialArea = static_cast<std::uint64_t>(std::llround(area / 2.0L));
  return result;
}

/// Сравнивает геометрию ориентаций без учёта исходного угла.
bool sameGeometry(const PolygonOrientation & lhs, const PolygonOrientation & rhs)
{
  return lhs.outer == rhs.outer && lhs.holes == rhs.holes;
}

} // namespace internal

using namespace internal;

/// Создаёт среду с исправленным каталогом, сохраняя прежнюю форму вызова.
std::unique_ptr<PolygonEnvironment> PolygonEnvironment::Create(const PolygonProblem & problem, std::string & error)
{
  return Create(problem, PolygonActionCatalogVersion::Corrected, error);
}

/// Нормализует все пути, проверяет топологию и создаёт уникальные ориентации.
std::unique_ptr<PolygonEnvironment> PolygonEnvironment::Create(const PolygonProblem & problem,
                                                               PolygonActionCatalogVersion catalogVersion, std::string & error)
{
  if (catalogVersion != PolygonActionCatalogVersion::Legacy && catalogVersion != PolygonActionCatalogVersion::Corrected)
  {
    error = "неподдерживаемая версия полигонального каталога действий";
    return nullptr;
  }
  const ValidationResult basic = validatePolygonProblem(problem);
  if (!basic.success)
  {
    error = basic.error;
    return nullptr;
  }
  std::int64_t width = 0;
  std::int64_t height = 0;
  std::int64_t margin = 0;
  std::int64_t spacing = 0;
  if (!toMicrons(problem.sheet.width, width) || !toMicrons(problem.sheet.height, height) ||
      !toMicrons(problem.manufacturing.sheetMargin, margin) || !toMicrons(problem.manufacturing.partSpacing, spacing) ||
      width <= 0 || height <= 0 || width > MAX_SHEET_UM || height > MAX_SHEET_UM)
  {
    error = "sheet is outside normalized M4 limits";
    return nullptr;
  }

  std::vector<std::vector<PolygonOrientation>> allOrientations;
  std::vector<PolygonPartInstance> instances;
  for (std::size_t partIndex = 0; partIndex < problem.parts.size(); ++partIndex)
  {
    const PolygonPart & part = problem.parts[partIndex];
    std::size_t remainingVertices = MAX_VERTICES;
    PolygonRing64 outer;
    if (!flattenPath(part.outer, problem.manufacturing.curveTolerance, outer, remainingVertices, error))
      return nullptr;
    std::vector<PolygonRing64> holes;
    std::size_t vertexCount = outer.size();
    for (const PolygonPath & path : part.holes)
    {
      PolygonRing64 hole;
      if (!flattenPath(path, problem.manufacturing.curveTolerance, hole, remainingVertices, error))
        return nullptr;
      vertexCount += hole.size();
      holes.push_back(std::move(hole));
    }
    if (vertexCount > MAX_VERTICES || !localizeRings(outer, holes))
    {
      error = "polygon part extent or vertex count is outside M4 limits";
      return nullptr;
    }
    if (!simpleRing(outer) || signedDoubleArea(outer) == 0)
    {
      if (error.empty())
        error = "outer ring is degenerate or self-intersecting";
      return nullptr;
    }
    if (signedDoubleArea(outer) < 0)
      std::reverse(outer.begin(), outer.end());
    std::vector<PolygonRing64> acceptedHoles;
    for (PolygonRing64 & hole : holes)
    {
      if (!simpleRing(hole) || signedDoubleArea(hole) == 0 || !ringsDisjoint(outer, hole) ||
          pointInRing(hole.front(), outer) != 1)
      {
        if (error.empty())
          error = "hole must be simple and strictly inside outer ring";
        return nullptr;
      }
      for (const PolygonRing64 & previous : acceptedHoles)
        if (!ringsDisjoint(previous, hole) || pointInRing(hole.front(), previous) >= 0 ||
            pointInRing(previous.front(), hole) >= 0)
        {
          error = "holes must be disjoint";
          return nullptr;
        }
      if (signedDoubleArea(hole) > 0)
        std::reverse(hole.begin(), hole.end());
      acceptedHoles.push_back(std::move(hole));
    }
    holes = std::move(acceptedHoles);
    std::vector<PolygonOrientation> orientations;
    for (int rotation : part.allowedRotations)
    {
      PolygonOrientation orientation = makeOrientation(outer, holes, rotation);
      if (orientation.width > MAX_SHEET_UM || orientation.height > MAX_SHEET_UM || orientation.materialArea == 0)
      {
        error = "polygon part extent or material area is outside M4 limits";
        return nullptr;
      }
      if (orientation.width + 2 * margin > width || orientation.height + 2 * margin > height)
      {
        // Ориентация сохраняется: другая может поместиться, а валидатор обязан
        // отличать невозможный экземпляр от некорректной исходной геометрии.
      }
      if (std::none_of(orientations.begin(), orientations.end(),
                       [&orientation](const auto & item) { return sameGeometry(item, orientation); }))
        orientations.push_back(std::move(orientation));
    }
    const PolygonOrientation & canonical = orientations.front();
    for (std::uint32_t instanceIndex = 0; instanceIndex < part.quantity; ++instanceIndex)
      instances.push_back({partIndex, instanceIndex, canonical.materialArea, std::max(canonical.width, canonical.height)});
    allOrientations.push_back(std::move(orientations));
  }
  return std::unique_ptr<PolygonEnvironment>(new PolygonEnvironment(
    problem, width, height, margin, spacing, std::move(allOrientations), std::move(instances), catalogVersion));
}

} // namespace aipackaging::solver
