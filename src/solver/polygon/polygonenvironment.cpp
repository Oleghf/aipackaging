#include <utility>

#include "polygoninternal.h"


namespace aipackaging::solver
{
using namespace internal;

/// Сохраняет подготовленные данные без повторной геометрической обработки.
PolygonEnvironment::PolygonEnvironment(PolygonProblem problem, std::int64_t sheetWidth, std::int64_t sheetHeight,
                                       std::int64_t sheetMargin, std::int64_t partSpacing,
                                       std::vector<std::vector<PolygonOrientation>> orientations,
                                       std::vector<PolygonPartInstance> instances, PolygonActionCatalogVersion catalogVersion)
  : problem_(std::move(problem))
  , sheetWidth_(sheetWidth)
  , sheetHeight_(sheetHeight)
  , sheetMargin_(sheetMargin)
  , partSpacing_(partSpacing)
  , orientations_(std::move(orientations))
  , instances_(std::move(instances))
  , catalogVersion_(catalogVersion)
{
}

/// Возвращает подготовленные ориентации с проверкой индекса стандартным исключением вектора.
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

/// Находит ориентацию размещения и выполняет целочисленный перенос внешнего кольца.
PolygonRing64 PolygonEnvironment::placedOuter(const PolygonPlacement & placement) const
{
  const std::size_t instancePosition = findInstance(placement.partId, placement.instanceIndex);
  if (instancePosition == instances_.size())
    return {};
  const PolygonOrientation * orientation = findOrientation(instances_[instancePosition].partIndex, placement.rotationDegrees);
  return orientation ? translateRing(orientation->outer, placement.x, placement.y) : PolygonRing64{};
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

} // namespace aipackaging::solver
