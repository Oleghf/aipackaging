#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

#include <aipackaging/nesting/polygon_environment.h>
#include <clipper2/clipper.h>

namespace aipackaging::solver
{
namespace
{
constexpr long double MICRONS_PER_MM = 1000.0L;
constexpr std::int64_t MAX_SHEET_UM = 10'000'000;
constexpr std::size_t MAX_INSTANCES = 100;
constexpr std::size_t MAX_VERTICES = 2000;
constexpr std::size_t MAX_CANDIDATES = 250000;
constexpr int RASTER_SIZE = 128;

using Wide = long double;

/// Округляет миллиметры к авторитетной микронной координате.
bool toMicrons(double value, std::int64_t & result)
{
  if (!std::isfinite(value))
    return false;
  const Wide scaled = static_cast<Wide>(value) * MICRONS_PER_MM;
  if (scaled < static_cast<Wide>(std::numeric_limits<std::int64_t>::min()) ||
      scaled > static_cast<Wide>(std::numeric_limits<std::int64_t>::max()))
    return false;
  result = static_cast<std::int64_t>(std::llround(scaled));
  return true;
}

/// Сравнивает исходные точки с микронным допуском замыкания.
bool sourceEqual(const PolygonPointMm & lhs, const PolygonPointMm & rhs)
{
  return std::abs(lhs.x - rhs.x) <= 0.0005 && std::abs(lhs.y - rhs.y) <= 0.0005;
}

/// Возвращает знак ориентированной площади треугольника без int64-переполнения.
Wide cross(const PolygonPoint64 & a, const PolygonPoint64 & b, const PolygonPoint64 & c)
{
  return static_cast<Wide>(b.x - a.x) * static_cast<Wide>(c.y - a.y) -
         static_cast<Wide>(b.y - a.y) * static_cast<Wide>(c.x - a.x);
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
  const Wide dx = static_cast<Wide>(b.x - a.x);
  const Wide dy = static_cast<Wide>(b.y - a.y);
  const Wide lengthSquared = dx * dx + dy * dy;
  if (lengthSquared == 0)
  {
    const Wide px = static_cast<Wide>(point.x - a.x);
    const Wide py = static_cast<Wide>(point.y - a.y);
    return px * px + py * py;
  }
  const Wide projection =
    std::clamp((static_cast<Wide>(point.x - a.x) * dx + static_cast<Wide>(point.y - a.y) * dy) / lengthSquared, 0.0L, 1.0L);
  const Wide px = static_cast<Wide>(point.x - a.x) - projection * dx;
  const Wide py = static_cast<Wide>(point.y - a.y) - projection * dy;
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
      const Wide intersectionX =
        static_cast<Wide>(b.x - a.x) * static_cast<Wide>(point.y - a.y) / static_cast<Wide>(b.y - a.y) + static_cast<Wide>(a.x);
      if (static_cast<Wide>(point.x) < intersectionX)
        inside = !inside;
    }
  }
  return inside ? 1 : -1;
}

/// Сообщает о положительном перекрытии заполненных внешних колец; касание допускается.
bool interiorsOverlap(const PolygonRing64 & lhs, const PolygonRing64 & rhs)
{
  Clipper2Lib::Path64 left;
  Clipper2Lib::Path64 right;
  left.reserve(lhs.size());
  right.reserve(rhs.size());
  for (const PolygonPoint64 & point : lhs)
    left.emplace_back(point.x, point.y);
  for (const PolygonPoint64 & point : rhs)
    right.emplace_back(point.x, point.y);
  const Clipper2Lib::Paths64 intersection =
    Clipper2Lib::Intersect(Clipper2Lib::Paths64{left}, Clipper2Lib::Paths64{right}, Clipper2Lib::FillRule::NonZero);
  return std::any_of(intersection.begin(), intersection.end(),
                     [](const Clipper2Lib::Path64 & path) { return std::abs(Clipper2Lib::Area(path)) > 0.0; });
}

/// Добавляет точку кривой после округления, не создавая последовательных дублей.
bool appendPoint(PolygonRing64 & ring, const PolygonPointMm & point)
{
  PolygonPoint64 normalized;
  if (!toMicrons(point.x, normalized.x) || !toMicrons(point.y, normalized.y))
    return false;
  if (ring.empty() || !(ring.back() == normalized))
    ring.push_back(normalized);
  return true;
}

/// Возвращает расстояние контрольной точки до бесконечной прямой хорды в миллиметрах.
double pointLineDistance(const PolygonPointMm & point, const PolygonPointMm & start, const PolygonPointMm & end)
{
  const double dx = end.x - start.x;
  const double dy = end.y - start.y;
  const double length = std::hypot(dx, dy);
  if (length == 0.0)
    return std::hypot(point.x - start.x, point.y - start.y);
  return std::abs(dx * (start.y - point.y) - (start.x - point.x) * dy) / length;
}

/// Делит Bézier слева направо, пока обе контрольные точки не лежат в tolerance от хорды.
bool flattenBezier(const PolygonPointMm & p0, const PolygonPointMm & p1, const PolygonPointMm & p2, const PolygonPointMm & p3,
                   double tolerance, int depth, PolygonRing64 & result)
{
  if ((pointLineDistance(p1, p0, p3) <= tolerance && pointLineDistance(p2, p0, p3) <= tolerance) || depth >= 20)
    return appendPoint(result, p3);
  const PolygonPointMm p01{(p0.x + p1.x) / 2.0, (p0.y + p1.y) / 2.0};
  const PolygonPointMm p12{(p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0};
  const PolygonPointMm p23{(p2.x + p3.x) / 2.0, (p2.y + p3.y) / 2.0};
  const PolygonPointMm p012{(p01.x + p12.x) / 2.0, (p01.y + p12.y) / 2.0};
  const PolygonPointMm p123{(p12.x + p23.x) / 2.0, (p12.y + p23.y) / 2.0};
  const PolygonPointMm middle{(p012.x + p123.x) / 2.0, (p012.y + p123.y) / 2.0};
  return flattenBezier(p0, p01, p012, middle, tolerance, depth + 1, result) &&
         flattenBezier(middle, p123, p23, p3, tolerance, depth + 1, result);
}

/// Аппроксимирует один исходный путь с заданной ошибкой и микронным округлением.
bool flattenPath(const PolygonPath & path, double tolerance, PolygonRing64 & result, std::string & error)
{
  if (path.segments.empty() || !appendPoint(result, path.start))
  {
    error = "polygon path is empty or contains invalid coordinates";
    return false;
  }
  PolygonPointMm current = path.start;
  for (const PolygonSegment & segment : path.segments)
  {
    if (segment.kind == PolygonSegmentKind::Line)
    {
      if (!appendPoint(result, segment.end))
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
      const std::size_t pieces = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(std::abs(sweep) / maxAngle)));
      for (std::size_t index = 1; index <= pieces; ++index)
      {
        const double angle = startAngle + sweep * static_cast<double>(index) / static_cast<double>(pieces);
        const PolygonPointMm point{segment.center.x + radius * std::cos(angle), segment.center.y + radius * std::sin(angle)};
        if (!appendPoint(result, index == pieces ? segment.end : point))
        {
          error = "arc approximation is outside coordinate range";
          return false;
        }
      }
    }
    else
    {
      if (!flattenBezier(current, segment.control1, segment.control2, segment.end, tolerance, 0, result))
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
    result.pop_back();

  // Коллинеарные вершины не несут геометрии, но резко увеличивают NFP-каталог.
  bool changed = true;
  while (changed && result.size() >= 3)
  {
    changed = false;
    for (std::size_t index = 0; index < result.size(); ++index)
    {
      if (cross(result[(index + result.size() - 1) % result.size()], result[index], result[(index + 1) % result.size()]) == 0)
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
    area += static_cast<Wide>(a.x) * b.y - static_cast<Wide>(b.x) * a.y;
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

/// Переносит кольцо в координаты листа.
PolygonRing64 translateRing(const PolygonRing64 & ring, std::int64_t x, std::int64_t y)
{
  PolygonRing64 result = ring;
  for (PolygonPoint64 & point : result)
  {
    point.x += x;
    point.y += y;
  }
  return result;
}

/// Консервативно помечает растровую клетку занятой при любом контакте с outer proxy.
bool rasterCellOccupied(const PolygonEnvironment & environment, const PolygonState & state, std::int64_t minX, std::int64_t minY,
                        std::int64_t maxX, std::int64_t maxY)
{
  const std::array<PolygonPoint64, 4> corners{{{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}}};
  for (const PolygonPlacement & placement : state.placements)
  {
    const PolygonRing64 ring = environment.placedOuter(placement);
    if (std::any_of(corners.begin(), corners.end(),
                    [&ring](const PolygonPoint64 & point) { return pointInRing(point, ring) >= 0; }))
      return true;
    if (std::any_of(ring.begin(), ring.end(), [=](const PolygonPoint64 & point)
                    { return point.x >= minX && point.x <= maxX && point.y >= minY && point.y <= maxY; }))
      return true;
    for (std::size_t edge = 0; edge < ring.size(); ++edge)
      for (std::size_t side = 0; side < corners.size(); ++side)
        if (segmentIntersection(ring[edge], ring[(edge + 1) % ring.size()], corners[side],
                                corners[(side + 1) % corners.size()]) != 0)
          return true;
  }
  return false;
}
} // namespace

/// Проверяет исходные скаляры и ограничения, не выполняя полную нормализацию кривых.
ValidationResult validatePolygonProblem(const PolygonProblem & problem)
{
  if (problem.problemId.empty())
    return {false, "problemId must not be empty"};
  if (problem.sheet.unit != "mm" || !std::isfinite(problem.sheet.width) || !std::isfinite(problem.sheet.height) ||
      problem.sheet.width <= 0 || problem.sheet.height <= 0 || problem.sheet.width > 10000 || problem.sheet.height > 10000)
    return {false, "sheet must use mm and fit the M4 size limit"};
  const PolygonManufacturing & m = problem.manufacturing;
  if (!std::isfinite(m.sheetMargin) || !std::isfinite(m.partSpacing) || !std::isfinite(m.kerf) ||
      !std::isfinite(m.curveTolerance) || m.sheetMargin < 0 || m.partSpacing < 0 || m.kerf < 0 || m.sheetMargin > 10000 ||
      m.partSpacing > 10000 || m.kerf > 10000 || m.curveTolerance < 0.001 || m.curveTolerance > 0.05 ||
      2.0 * m.sheetMargin >= std::min(problem.sheet.width, problem.sheet.height))
    return {false, "invalid manufacturing parameters"};
  if (problem.parts.empty() || problem.objective.type != "valuable_right_remnant" || problem.objective.version != 1)
    return {false, "parts must not be empty and objective must be valuable_right_remnant v1"};
  std::set<std::string> ids;
  std::size_t instances = 0;
  for (const PolygonPart & part : problem.parts)
  {
    if (part.id.empty() || !ids.insert(part.id).second || part.quantity == 0 || part.allowedRotations.empty())
      return {false, "part ids, quantities and rotations must be valid and unique"};
    instances += part.quantity;
    std::set<int> rotations;
    for (int rotation : part.allowedRotations)
      if ((rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) || !rotations.insert(rotation).second)
        return {false, "allowedRotations must be a unique subset of 0/90/180/270"};
  }
  if (instances > MAX_INSTANCES)
    return {false, "polygon problem exceeds 100 instances"};
  return {true, {}};
}

/// Нормализует все пути, проверяет топологию и создаёт уникальные ориентации.
std::unique_ptr<PolygonEnvironment> PolygonEnvironment::Create(const PolygonProblem & problem, std::string & error)
{
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
    PolygonRing64 outer;
    if (!flattenPath(part.outer, problem.manufacturing.curveTolerance, outer, error) || !simpleRing(outer) ||
        signedDoubleArea(outer) == 0)
    {
      if (error.empty())
        error = "outer ring is degenerate or self-intersecting";
      return nullptr;
    }
    if (signedDoubleArea(outer) < 0)
      std::reverse(outer.begin(), outer.end());
    std::vector<PolygonRing64> holes;
    std::size_t vertexCount = outer.size();
    for (const PolygonPath & path : part.holes)
    {
      PolygonRing64 hole;
      if (!flattenPath(path, problem.manufacturing.curveTolerance, hole, error) || !simpleRing(hole) ||
          signedDoubleArea(hole) == 0 || !ringsDisjoint(outer, hole) || pointInRing(hole.front(), outer) != 1)
      {
        if (error.empty())
          error = "hole must be simple and strictly inside outer ring";
        return nullptr;
      }
      for (const PolygonRing64 & previous : holes)
        if (!ringsDisjoint(previous, hole) || pointInRing(hole.front(), previous) >= 0 ||
            pointInRing(previous.front(), hole) >= 0)
        {
          error = "holes must be disjoint";
          return nullptr;
        }
      if (signedDoubleArea(hole) > 0)
        std::reverse(hole.begin(), hole.end());
      vertexCount += hole.size();
      holes.push_back(std::move(hole));
    }
    if (vertexCount > MAX_VERTICES)
    {
      error = "polygon part exceeds 2000 vertices after approximation";
      return nullptr;
    }
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
  return std::unique_ptr<PolygonEnvironment>(
    new PolygonEnvironment(problem, width, height, margin, spacing, std::move(allOrientations), std::move(instances)));
}

/// Сохраняет подготовленные данные без повторной геометрической обработки.
PolygonEnvironment::PolygonEnvironment(PolygonProblem problem, std::int64_t sheetWidth, std::int64_t sheetHeight,
                                       std::int64_t sheetMargin, std::int64_t partSpacing,
                                       std::vector<std::vector<PolygonOrientation>> orientations,
                                       std::vector<PolygonPartInstance> instances)
  : problem_(std::move(problem))
  , sheetWidth_(sheetWidth)
  , sheetHeight_(sheetHeight)
  , sheetMargin_(sheetMargin)
  , partSpacing_(partSpacing)
  , orientations_(std::move(orientations))
  , instances_(std::move(instances))
{
}

/// Возвращает подготовленные ориентации с проверкой индекса стандартным исключением vector.
const std::vector<PolygonOrientation> & PolygonEnvironment::orientations(std::size_t partIndex) const
{
  return orientations_.at(partIndex);
}

/// Инициализирует флаги экземпляров и пустую последовательность размещений.
PolygonState PolygonEnvironment::initialState() const
{
  PolygonState result;
  result.placedInstances.resize(instances_.size(), 0);
  return result;
}

/// Линейно находит стабильную позицию небольшого списка экземпляров.
std::size_t PolygonEnvironment::findInstance(const std::string & partId, std::uint32_t instanceIndex) const
{
  for (std::size_t index = 0; index < instances_.size(); ++index)
  {
    const PolygonPartInstance & instance = instances_[index];
    if (problem_.parts[instance.partIndex].id == partId && instance.instanceIndex == instanceIndex)
      return index;
  }
  return instances_.size();
}

/// Ищет угол среди дедуплицированных ориентаций типа детали.
const PolygonOrientation * PolygonEnvironment::findOrientation(std::size_t partIndex, int rotationDegrees) const
{
  if (partIndex >= orientations_.size())
    return nullptr;
  for (const PolygonOrientation & orientation : orientations_[partIndex])
    if (orientation.rotationDegrees == rotationDegrees)
      return &orientation;
  return nullptr;
}

/// Находит ориентацию размещения и выполняет целочисленный перенос outer ring.
PolygonRing64 PolygonEnvironment::placedOuter(const PolygonPlacement & placement) const
{
  const std::size_t instancePosition = findInstance(placement.partId, placement.instanceIndex);
  if (instancePosition == instances_.size())
    return {};
  const PolygonOrientation * orientation = findOrientation(instances_[instancePosition].partIndex, placement.rotationDegrees);
  return orientation ? translateRing(orientation->outer, placement.x, placement.y) : PolygonRing64{};
}

/// Проверяет экземпляр, границы, положительное перекрытие и евклидов зазор outer proxy.
bool PolygonEnvironment::canApply(const PolygonState & state, const PolygonAction & action) const
{
  const std::size_t instancePosition = findInstance(action.partId, action.instanceIndex);
  if (instancePosition >= instances_.size() || state.placedInstances.size() != instances_.size() ||
      state.placedInstances[instancePosition])
    return false;
  const PolygonOrientation * orientation = findOrientation(instances_[instancePosition].partIndex, action.rotationDegrees);
  if (!orientation)
    return false;
  const PolygonRing64 moving = translateRing(orientation->outer, action.x, action.y);
  for (const PolygonPoint64 & point : moving)
    if (point.x < sheetMargin_ || point.y < sheetMargin_ || point.x > sheetWidth_ - sheetMargin_ ||
        point.y > sheetHeight_ - sheetMargin_)
      return false;
  const Wide requiredSquared = static_cast<Wide>(partSpacing_) * partSpacing_;
  for (const PolygonPlacement & placement : state.placements)
  {
    const PolygonRing64 fixed = placedOuter(placement);
    if (interiorsOverlap(moving, fixed) || (partSpacing_ > 0 && ringDistanceSquared(moving, fixed) < requiredSquared))
      return false;
  }
  return true;
}

/// Помечает экземпляр и добавляет размещение только после полной canApply-проверки.
bool PolygonEnvironment::apply(PolygonState & state, const PolygonAction & action) const
{
  if (!canApply(state, action))
    return false;
  const std::size_t instancePosition = findInstance(action.partId, action.instanceIndex);
  state.placedInstances[instancePosition] = 1;
  state.placements.push_back(action);
  state.placedArea += instances_[instancePosition].area;
  state.objectiveCached = false;
  return true;
}

/// Формирует точки контакта с листом и вершинами размещённых outer proxy.
std::vector<PolygonAction> PolygonEnvironment::enumerateCandidates(const PolygonState & state, std::size_t instancePosition) const
{
  std::vector<PolygonAction> result;
  if (instancePosition >= instances_.size() || state.placedInstances[instancePosition])
    return result;
  const PolygonPartInstance & instance = instances_[instancePosition];
  const PolygonPart & part = problem_.parts[instance.partIndex];
  std::set<std::tuple<int, std::int64_t, std::int64_t>> unique;
  for (const PolygonOrientation & orientation : orientations_[instance.partIndex])
  {
    const std::int64_t left = sheetMargin_;
    const std::int64_t bottom = sheetMargin_;
    const std::int64_t right = sheetWidth_ - sheetMargin_ - orientation.width;
    const std::int64_t top = sheetHeight_ - sheetMargin_ - orientation.height;
    if (right < left || top < bottom)
      continue;
    if (right == left || top == bottom)
    {
      // Вырожденная inner-fit область является отрезком либо точкой. Clipper2
      // не возвращает её как полигон с площадью, поэтому явно сохраняем
      // граничные переносы и ниже пропускаем их через точный валидатор.
      unique.emplace(orientation.rotationDegrees, left, bottom);
      unique.emplace(orientation.rotationDegrees, right, bottom);
      unique.emplace(orientation.rotationDegrees, left, top);
      unique.emplace(orientation.rotationDegrees, right, top);
      continue;
    }
    Clipper2Lib::Path64 feasibleRectangle{{left, bottom}, {right, bottom}, {right, top}, {left, top}};
    Clipper2Lib::Path64 reflectedMoving;
    reflectedMoving.reserve(orientation.outer.size());
    for (const PolygonPoint64 & point : orientation.outer)
      reflectedMoving.emplace_back(-point.x, -point.y);
    Clipper2Lib::Paths64 obstacles;
    for (const PolygonPlacement & fixedPlacement : state.placements)
    {
      const PolygonRing64 fixed = placedOuter(fixedPlacement);
      Clipper2Lib::Path64 fixedPath;
      fixedPath.reserve(fixed.size());
      for (const PolygonPoint64 & point : fixed)
        fixedPath.emplace_back(point.x, point.y);
      Clipper2Lib::Paths64 nfp = Clipper2Lib::MinkowskiSum(reflectedMoving, fixedPath, true);
      if (partSpacing_ > 0)
        nfp = Clipper2Lib::InflatePaths(nfp, static_cast<double>(partSpacing_), Clipper2Lib::JoinType::Round,
                                        Clipper2Lib::EndType::Polygon, 2.0, 1.0);
      obstacles.insert(obstacles.end(), nfp.begin(), nfp.end());
    }
    // Вычитание объединения NFP из inner-fit rectangle даёт конфигурационное
    // пространство переносов. Его вершины включают контакты и пересечения
    // ограничений, которые теряются при простом попарном совмещении вершин.
    const Clipper2Lib::Paths64 feasible = obstacles.empty() ? Clipper2Lib::Paths64{feasibleRectangle}
                                                            : Clipper2Lib::Difference(Clipper2Lib::Paths64{feasibleRectangle},
                                                                                      obstacles, Clipper2Lib::FillRule::NonZero);
    for (const Clipper2Lib::Path64 & boundary : feasible)
      for (const Clipper2Lib::Point64 & point : boundary)
        unique.emplace(orientation.rotationDegrees, point.x, point.y);
  }
  for (const auto & [rotation, x, y] : unique)
  {
    PolygonAction action{part.id, instance.instanceIndex, x, y, rotation};
    if (canApply(state, action))
      result.push_back(std::move(action));
    if (result.size() > MAX_CANDIDATES)
      throw std::length_error("polygon candidate catalog exceeds 250000 actions");
  }
  const std::int64_t currentUsed = evaluate(state).usedLength + sheetMargin_;
  std::stable_sort(result.begin(), result.end(),
                   [this, currentUsed, instancePosition](const auto & lhs, const auto & rhs)
                   {
                     const PolygonOrientation * lo = findOrientation(instances_[instancePosition].partIndex, lhs.rotationDegrees);
                     const PolygonOrientation * ro = findOrientation(instances_[instancePosition].partIndex, rhs.rotationDegrees);
                     return std::tuple(std::max(currentUsed, lhs.x + lo->width), lhs.y, lhs.x, lhs.rotationDegrees, lhs.partId,
                                       lhs.instanceIndex) < std::tuple(std::max(currentUsed, rhs.x + ro->width), rhs.y, rhs.x,
                                                                       rhs.rotationDegrees, rhs.partId, rhs.instanceIndex);
                   });
  return result;
}

/// Вычисляет primary по точным bounds и secondary по стабильной сетке 128x128.
PolygonObjectiveComponents PolygonEnvironment::evaluate(const PolygonState & state) const
{
  if (state.objectiveCached)
    return state.cachedObjective;
  PolygonObjectiveComponents result;
  result.placedParts = state.placements.size();
  result.totalParts = instances_.size();
  result.placedArea = state.placedArea;
  for (const PolygonPartInstance & instance : instances_)
    result.totalPartArea += instance.area;
  std::int64_t right = sheetMargin_;
  for (const PolygonPlacement & placement : state.placements)
  {
    const PolygonRing64 outer = placedOuter(placement);
    for (const PolygonPoint64 & point : outer)
      right = std::max(right, point.x);
  }
  const std::int64_t usableWidth = sheetWidth_ - 2 * sheetMargin_;
  const std::int64_t usableHeight = sheetHeight_ - 2 * sheetMargin_;
  result.usedLength = state.placements.empty() ? 0 : right - sheetMargin_;
  result.primaryRemnantWidth = usableWidth - result.usedLength;
  result.materialUtilization =
    usableWidth > 0 && usableHeight > 0
      ? static_cast<double>(state.placedArea) / (static_cast<double>(usableWidth) * static_cast<double>(usableHeight))
      : 0.0;

  const int usedColumns =
    usableWidth == 0 ? 0 : static_cast<int>((result.usedLength * RASTER_SIZE + usableWidth - 1) / usableWidth);
  std::vector<unsigned char> freeCells(static_cast<std::size_t>(RASTER_SIZE * RASTER_SIZE), 0);
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < usedColumns; ++column)
    {
      const std::int64_t minX = sheetMargin_ + static_cast<std::int64_t>(column) * usableWidth / RASTER_SIZE;
      const std::int64_t maxX = sheetMargin_ + static_cast<std::int64_t>(column + 1) * usableWidth / RASTER_SIZE;
      const std::int64_t minY = sheetMargin_ + static_cast<std::int64_t>(row) * usableHeight / RASTER_SIZE;
      const std::int64_t maxY = sheetMargin_ + static_cast<std::int64_t>(row + 1) * usableHeight / RASTER_SIZE;
      freeCells[static_cast<std::size_t>(row * RASTER_SIZE + column)] =
        rasterCellOccupied(*this, state, minX, minY, maxX, maxY) ? 0 : 1;
    }

  // Histogram-алгоритм находит крупнейший свободный осевой прямоугольник.
  std::array<int, RASTER_SIZE> heights{};
  std::size_t largestRectangleCells = 0;
  for (int row = 0; row < RASTER_SIZE; ++row)
  {
    for (int column = 0; column < RASTER_SIZE; ++column)
      heights[column] = freeCells[static_cast<std::size_t>(row * RASTER_SIZE + column)] ? heights[column] + 1 : 0;
    for (int left = 0; left < usedColumns; ++left)
    {
      int minimum = heights[left];
      for (int rightColumn = left; rightColumn < usedColumns && minimum > 0; ++rightColumn)
      {
        minimum = std::min(minimum, heights[rightColumn]);
        largestRectangleCells = std::max(largestRectangleCells, static_cast<std::size_t>(minimum * (rightColumn - left + 1)));
      }
    }
  }

  std::size_t freeCount = std::accumulate(freeCells.begin(), freeCells.end(), std::size_t{0});
  std::size_t largestComponent = 0;
  std::vector<unsigned char> visited(freeCells.size(), 0);
  constexpr std::array<std::pair<int, int>, 4> DIRECTIONS{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < usedColumns; ++column)
    {
      const std::size_t start = static_cast<std::size_t>(row * RASTER_SIZE + column);
      if (!freeCells[start] || visited[start])
        continue;
      std::queue<std::pair<int, int>> queue;
      queue.emplace(column, row);
      visited[start] = 1;
      std::size_t component = 0;
      while (!queue.empty())
      {
        const auto [x, y] = queue.front();
        queue.pop();
        ++component;
        for (const auto [dx, dy] : DIRECTIONS)
        {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx < 0 || nx >= usedColumns || ny < 0 || ny >= RASTER_SIZE)
            continue;
          const std::size_t next = static_cast<std::size_t>(ny * RASTER_SIZE + nx);
          if (freeCells[next] && !visited[next])
          {
            visited[next] = 1;
            queue.emplace(nx, ny);
          }
        }
      }
      largestComponent = std::max(largestComponent, component);
    }
  const Wide cellArea = static_cast<Wide>(usableWidth) * usableHeight / (RASTER_SIZE * RASTER_SIZE);
  result.largestExtraRectangleArea = static_cast<std::uint64_t>(std::llround(cellArea * largestRectangleCells));
  result.fragmentationPenalty = static_cast<std::uint64_t>(std::llround(cellArea * (freeCount - largestComponent)));
  state.cachedObjective = result;
  state.objectiveCached = true;
  return result;
}

/// Создаёт новую среду и повторно применяет placements, не доверяя solution-метрикам.
ValidationResult validatePolygonSolution(const PolygonProblem & problem, const PolygonSolution & solution)
{
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
    return {false, error};
  if (solution.problemId != problem.problemId)
    return {false, "solution problemId does not match problem"};
  PolygonState state = environment->initialState();
  for (const PolygonPlacement & placement : solution.placements)
    if (!environment->apply(state, placement))
      return {false, "solution contains an invalid polygon placement"};
  const PolygonObjectiveComponents actual = environment->evaluate(state);
  const PolygonObjectiveComponents & recorded = solution.objective;
  if (actual.usedLength != recorded.usedLength || actual.primaryRemnantWidth != recorded.primaryRemnantWidth ||
      actual.largestExtraRectangleArea != recorded.largestExtraRectangleArea ||
      actual.fragmentationPenalty != recorded.fragmentationPenalty || actual.placedParts != recorded.placedParts ||
      actual.totalParts != recorded.totalParts || actual.placedArea != recorded.placedArea ||
      actual.totalPartArea != recorded.totalPartArea ||
      std::abs(actual.materialUtilization - recorded.materialUtilization) > 1e-12 ||
      actual.rasterColumns != recorded.rasterColumns || actual.rasterRows != recorded.rasterRows)
    return {false, "polygon solution objective does not match geometry"};
  if ((solution.status == SolveStatus::Solved) != (actual.placedParts == actual.totalParts))
    return {false, "polygon solution status does not match completeness"};
  return {true, {}};
}
} // namespace aipackaging::solver
