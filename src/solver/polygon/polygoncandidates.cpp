#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

#include <clipper2/clipper.h>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
using CandidateKey = std::tuple<int, std::int64_t, std::int64_t>;

/// Описывает допустимый прямоугольник переноса нормализованной ориентации.
struct TranslationBounds
{
  std::int64_t left = 0;
  std::int64_t bottom = 0;
  std::int64_t right = 0;
  std::int64_t top = 0;

  /// Сообщает, существует ли непустая допустимая область переноса.
  bool valid() const { return right >= left && top >= bottom; }
  /// Сообщает, вырождена ли область в отрезок либо точку.
  bool degenerate() const { return right == left || top == bottom; }
};

/// Описывает одномерное сечение допустимой области переноса.
struct ContactSegment
{
  bool vertical = false;
  std::int64_t fixed = 0;
  std::int64_t low = 0;
  std::int64_t high = 0;
};

/// Добавляет переменную координату контакта, если она принадлежит допустимому отрезку.
void addContactCandidate(const ContactSegment & segment, int rotation, std::int64_t variable, std::set<CandidateKey> & unique)
{
  if (variable >= segment.low && variable <= segment.high)
    unique.emplace(rotation, segment.vertical ? segment.fixed : variable, segment.vertical ? variable : segment.fixed);
}

/// Добавляет соседние целые координаты дробного пересечения для последующей точной проверки.
void addRoundedCrossing(const ContactSegment & segment, int rotation, long double crossing, std::set<CandidateKey> & unique)
{
  if (crossing < static_cast<long double>(segment.low) - 1.0L || crossing > static_cast<long double>(segment.high) + 1.0L)
    return;
  // Соседние микронные позиции защищают от округления дробного пересечения;
  // геометрическая допустимость каждой затем проверяется отдельно.
  const long double floored = std::floor(crossing);
  if (floored < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
      floored > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
    return;
  const auto floorValue = static_cast<std::int64_t>(floored);
  for (std::int64_t offset = -1; offset <= 2; ++offset)
  {
    const long double candidate = static_cast<long double>(floorValue) + static_cast<long double>(offset);
    if (candidate >= static_cast<long double>(std::numeric_limits<std::int64_t>::min()) &&
        candidate <= static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
      addContactCandidate(segment, rotation, static_cast<std::int64_t>(candidate), unique);
  }
}

/// Добавляет контакты одного ребра NFP с допустимым отрезком переноса.
void addEdgeContacts(const ContactSegment & segment, int rotation, const Clipper2Lib::Point64 & a, const Clipper2Lib::Point64 & b,
                     std::set<CandidateKey> & unique)
{
  const std::int64_t aFixed = segment.vertical ? a.x : a.y;
  const std::int64_t bFixed = segment.vertical ? b.x : b.y;
  const std::int64_t aVariable = segment.vertical ? a.y : a.x;
  const std::int64_t bVariable = segment.vertical ? b.y : b.x;
  if (aFixed == bFixed)
  {
    if (aFixed == segment.fixed)
    {
      // Совпадающее ребро задаёт два возможных края свободного промежутка.
      addContactCandidate(segment, rotation, aVariable, unique);
      addContactCandidate(segment, rotation, bVariable, unique);
    }
    return;
  }
  if (segment.fixed < std::min(aFixed, bFixed) || segment.fixed > std::max(aFixed, bFixed))
    return;
  const long double fraction = (static_cast<long double>(segment.fixed) - static_cast<long double>(aFixed)) /
                               (static_cast<long double>(bFixed) - static_cast<long double>(aFixed));
  const long double crossing =
    static_cast<long double>(aVariable) + fraction * (static_cast<long double>(bVariable) - static_cast<long double>(aVariable));
  addRoundedCrossing(segment, rotation, crossing, unique);
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

/// Добавляет микронные точки контакта границы NFP с допустимым отрезком переноса.
void addLineContacts(const Clipper2Lib::Paths64 & obstacles, const ContactSegment & segment, int rotation,
                     std::set<CandidateKey> & unique)
{
  addContactCandidate(segment, rotation, segment.low, unique);
  addContactCandidate(segment, rotation, segment.high, unique);
  for (const Clipper2Lib::Path64 & path : obstacles)
    for (std::size_t index = 0; index < path.size(); ++index)
      addEdgeContacts(segment, rotation, path[index], path[(index + 1) % path.size()], unique);
}

/// Вычисляет область переноса ориентации внутри листа с учётом отступа.
TranslationBounds translationBounds(const PolygonEnvironment & environment, const PolygonOrientation & orientation)
{
  return {environment.sheetMargin(), environment.sheetMargin(),
          environment.sheetWidth() - environment.sheetMargin() - orientation.width,
          environment.sheetHeight() - environment.sheetMargin() - orientation.height};
}

/// Формирует отражённый внешний контур движущейся ориентации для суммы Минковского.
Clipper2Lib::Path64 reflectedOuter(const PolygonOrientation & orientation)
{
  Clipper2Lib::Path64 result;
  result.reserve(orientation.outer.size());
  for (const PolygonPoint64 & point : orientation.outer)
    result.emplace_back(-point.x, -point.y);
  return result;
}

/// Строит объединяемый список NFP для всех уже размещённых деталей.
Clipper2Lib::Paths64 buildObstacles(const PolygonEnvironment & environment, const PolygonState & state,
                                    const PolygonOrientation & orientation)
{
  const Clipper2Lib::Path64 moving = reflectedOuter(orientation);
  Clipper2Lib::Paths64 obstacles;
  for (const PolygonPlacement & fixedPlacement : state.placements)
  {
    const PolygonRing64 fixed = environment.placedOuter(fixedPlacement);
    Clipper2Lib::Path64 fixedPath;
    fixedPath.reserve(fixed.size());
    for (const PolygonPoint64 & point : fixed)
      fixedPath.emplace_back(point.x, point.y);
    Clipper2Lib::Paths64 nfp = Clipper2Lib::MinkowskiSum(moving, fixedPath, true);
    if (environment.partSpacing() > 0)
      nfp = Clipper2Lib::InflatePaths(nfp, static_cast<double>(environment.partSpacing()), Clipper2Lib::JoinType::Round,
                                      Clipper2Lib::EndType::Polygon, 2.0, 1.0);
    obstacles.insert(obstacles.end(), nfp.begin(), nfp.end());
  }
  return obstacles;
}

/// Добавляет прежние граничные точки вырожденного каталога v1.
void addLegacyDegenerateBounds(const TranslationBounds & bounds, int rotation, std::set<CandidateKey> & unique)
{
  unique.emplace(rotation, bounds.left, bounds.bottom);
  unique.emplace(rotation, bounds.right, bounds.bottom);
  unique.emplace(rotation, bounds.left, bounds.top);
  unique.emplace(rotation, bounds.right, bounds.top);
}

/// Добавляет контакты исправленного вырожденного каталога v2.
void addCorrectedDegenerateContacts(const TranslationBounds & bounds, const Clipper2Lib::Paths64 & obstacles, int rotation,
                                    std::set<CandidateKey> & unique)
{
  if (bounds.right == bounds.left && bounds.top == bounds.bottom)
  {
    unique.emplace(rotation, bounds.left, bounds.bottom);
    return;
  }
  const bool vertical = bounds.right == bounds.left;
  const ContactSegment segment{vertical, vertical ? bounds.left : bounds.bottom, vertical ? bounds.bottom : bounds.left,
                               vertical ? bounds.top : bounds.right};
  addLineContacts(obstacles, segment, rotation, unique);
}

/// Вычитает NFP из прямоугольной области и добавляет вершины её границ.
void addFeasibleBoundaryContacts(const TranslationBounds & bounds, const Clipper2Lib::Paths64 & obstacles, int rotation,
                                 std::set<CandidateKey> & unique)
{
  Clipper2Lib::Path64 feasibleRectangle{{bounds.left, bounds.bottom},
                                        {bounds.right, bounds.bottom},
                                        {bounds.right, bounds.top},
                                        {bounds.left, bounds.top}};
  // Вычитание объединения NFP из допустимого прямоугольника даёт конфигурационное
  // пространство переносов и сохраняет пересечения ограничений как вершины.
  const Clipper2Lib::Paths64 feasible = obstacles.empty() ? Clipper2Lib::Paths64{feasibleRectangle}
                                                          : Clipper2Lib::Difference(Clipper2Lib::Paths64{feasibleRectangle},
                                                                                    obstacles, Clipper2Lib::FillRule::NonZero);
  for (const Clipper2Lib::Path64 & boundary : feasible)
    for (const Clipper2Lib::Point64 & point : boundary)
      unique.emplace(rotation, point.x, point.y);
}

/// Преобразует уникальные ключи в действия и оставляет только точно допустимые.
std::vector<PolygonAction> filterExactCandidates(const PolygonEnvironment & environment, const PolygonState & state,
                                                 const PolygonPart & part, const PolygonPartInstance & instance,
                                                 const std::set<CandidateKey> & unique)
{
  std::vector<PolygonAction> result;
  for (const auto & [rotation, x, y] : unique)
  {
    PolygonAction action{part.id, instance.instanceIndex, x, y, rotation};
    if (environment.canApply(state, action))
      result.push_back(std::move(action));
    if (result.size() > MAX_CANDIDATES)
      throw std::length_error("каталог вариантов полигонального размещения превышает 250000 действий");
  }
  return result;
}

/// Стабильно сортирует действия по прежнему ключу левого нижнего размещения.
void sortCandidates(const PolygonEnvironment & environment, const PolygonState & state, std::size_t instancePosition,
                    std::vector<PolygonAction> & result)
{
  const std::int64_t currentUsed = environment.evaluate(state).usedLength + environment.sheetMargin();
  std::stable_sort(result.begin(), result.end(),
                   [&environment, currentUsed, instancePosition](const auto & lhs, const auto & rhs)
                   {
                     const PolygonOrientation * lo =
                       environment.findOrientation(environment.instances()[instancePosition].partIndex, lhs.rotationDegrees);
                     const PolygonOrientation * ro =
                       environment.findOrientation(environment.instances()[instancePosition].partIndex, rhs.rotationDegrees);
                     return std::tuple(std::max(currentUsed, lhs.x + lo->width), lhs.y, lhs.x, lhs.rotationDegrees, lhs.partId,
                                       lhs.instanceIndex) < std::tuple(std::max(currentUsed, rhs.x + ro->width), rhs.y, rhs.x,
                                                                       rhs.rotationDegrees, rhs.partId, rhs.instanceIndex);
                   });
}

} // namespace internal

using namespace internal;

/// Формирует точки контакта с листом и вершинами внешних заменяющих контуров размещённых деталей.
std::vector<PolygonAction> PolygonEnvironment::enumerateCandidates(const PolygonState & state, std::size_t instancePosition) const
{
  if (instancePosition >= instances_.size() || state.placedInstances[instancePosition])
    return {};
  const PolygonPartInstance & instance = instances_[instancePosition];
  const PolygonPart & part = problem_.parts[instance.partIndex];
  std::set<CandidateKey> unique;
  for (const PolygonOrientation & orientation : orientations_[instance.partIndex])
  {
    const TranslationBounds bounds = translationBounds(*this, orientation);
    if (!bounds.valid())
      continue;
    if (bounds.degenerate() && catalogVersion_ == PolygonActionCatalogVersion::Legacy)
    {
      // Вырожденная область допустимого внутреннего размещения является отрезком либо точкой. Clipper2
      // не возвращает её как полигон с площадью, поэтому явно сохраняем
      // граничные переносы и ниже пропускаем их через точный валидатор.
      addLegacyDegenerateBounds(bounds, orientation.rotationDegrees, unique);
      continue;
    }
    const Clipper2Lib::Paths64 obstacles = buildObstacles(*this, state, orientation);
    if (bounds.degenerate())
      addCorrectedDegenerateContacts(bounds, obstacles, orientation.rotationDegrees, unique);
    else
      addFeasibleBoundaryContacts(bounds, obstacles, orientation.rotationDegrees, unique);
  }
  std::vector<PolygonAction> result = filterExactCandidates(*this, state, part, instance, unique);
  sortCandidates(*this, state, instancePosition, result);
  return result;
}

} // namespace aipackaging::solver
