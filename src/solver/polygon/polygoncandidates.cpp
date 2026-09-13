#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>

#include <clipper2/clipper.h>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
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

} // namespace internal

using namespace internal;

/// Формирует точки контакта с листом и вершинами внешних заменяющих контуров размещённых деталей.
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
      // Вырожденная область допустимого внутреннего размещения является отрезком либо точкой. Clipper2
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
    // Вычитание объединения NFP из прямоугольной области допустимого внутреннего размещения даёт конфигурационное
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
      throw std::length_error("каталог вариантов полигонального размещения превышает 250000 действий");
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

} // namespace aipackaging::solver
