#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
inline constexpr std::size_t RASTER_CELL_COUNT = static_cast<std::size_t>(RASTER_SIZE) * RASTER_SIZE;

/// Хранит физические размеры и занятую ширину растровой области.
struct RasterGeometry
{
  std::int64_t usableWidth = 0;
  std::int64_t usableHeight = 0;
  int usedColumns = 0;
};

/// Возвращает линейный индекс растровой клетки после расширения операндов.
std::size_t rasterOffset(int row, int column)
{
  return static_cast<std::size_t>(row) * static_cast<std::size_t>(RASTER_SIZE) + static_cast<std::size_t>(column);
}

/// Консервативно помечает растровую клетку занятой при любом контакте с внешним заменяющим контуром.
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

/// Вычисляет число использованных столбцов без переполняющего сложения при округлении вверх.
int usedRasterColumns(std::int64_t usedLength, std::int64_t usableWidth)
{
  if (usableWidth <= 0 || usedLength <= 0)
    return 0;
  const std::int64_t numerator = usedLength * static_cast<std::int64_t>(RASTER_SIZE);
  return static_cast<int>(numerator / usableWidth + (numerator % usableWidth == 0 ? 0 : 1));
}

/// Строит растровую маску свободных клеток в занятой части листа.
std::vector<unsigned char> rasterizeFreeCells(const PolygonEnvironment & environment, const PolygonState & state,
                                              const RasterGeometry & geometry)
{
  std::vector<unsigned char> freeCells(RASTER_CELL_COUNT, 0);
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < geometry.usedColumns; ++column)
    {
      const std::int64_t minX =
        environment.sheetMargin() + static_cast<std::int64_t>(column) * geometry.usableWidth / RASTER_SIZE;
      const std::int64_t maxX =
        environment.sheetMargin() + static_cast<std::int64_t>(column + 1) * geometry.usableWidth / RASTER_SIZE;
      const std::int64_t minY = environment.sheetMargin() + static_cast<std::int64_t>(row) * geometry.usableHeight / RASTER_SIZE;
      const std::int64_t maxY =
        environment.sheetMargin() + static_cast<std::int64_t>(row + 1) * geometry.usableHeight / RASTER_SIZE;
      freeCells[rasterOffset(row, column)] = rasterCellOccupied(environment, state, minX, minY, maxX, maxY) ? 0 : 1;
    }
  return freeCells;
}

/// Находит площадь крупнейшего свободного осевого прямоугольника в клетках.
std::size_t largestFreeRectangle(const std::vector<unsigned char> & freeCells, int usedColumns)
{
  std::array<int, RASTER_SIZE> heights{};
  std::size_t largestRectangleCells = 0;
  for (int row = 0; row < RASTER_SIZE; ++row)
  {
    for (int column = 0; column < RASTER_SIZE; ++column)
      heights[column] = freeCells[rasterOffset(row, column)] ? heights[column] + 1 : 0;
    for (int left = 0; left < usedColumns; ++left)
    {
      int minimum = heights[left];
      for (int right = left; right < usedColumns && minimum > 0; ++right)
      {
        minimum = std::min(minimum, heights[right]);
        const std::size_t width = static_cast<std::size_t>(right) - static_cast<std::size_t>(left) + static_cast<std::size_t>(1);
        largestRectangleCells = std::max(largestRectangleCells, static_cast<std::size_t>(minimum) * width);
      }
    }
  }
  return largestRectangleCells;
}

/// Возвращает число свободных клеток и размер их крупнейшей связной компоненты.
std::pair<std::size_t, std::size_t> measureFreeComponents(const std::vector<unsigned char> & freeCells, int usedColumns)
{
  const std::size_t freeCount = std::accumulate(freeCells.begin(), freeCells.end(), std::size_t{0});
  std::size_t largestComponent = 0;
  std::vector<unsigned char> visited(freeCells.size(), 0);
  constexpr std::array<std::pair<int, int>, 4> DIRECTIONS{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < usedColumns; ++column)
    {
      const std::size_t start = rasterOffset(row, column);
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
          const int nextX = x + dx;
          const int nextY = y + dy;
          if (nextX < 0 || nextX >= usedColumns || nextY < 0 || nextY >= RASTER_SIZE)
            continue;
          const std::size_t next = rasterOffset(nextY, nextX);
          if (freeCells[next] && !visited[next])
          {
            visited[next] = 1;
            queue.emplace(nextX, nextY);
          }
        }
      }
      largestComponent = std::max(largestComponent, component);
    }
  return {freeCount, largestComponent};
}

/// Переводит неотрицательное число растровых клеток в площадь с проверкой диапазона.
std::uint64_t rasterArea(Wide cellArea, std::size_t cells)
{
  const Wide value = cellArea * static_cast<Wide>(cells);
  if (!std::isfinite(value) || value < 0 || value > static_cast<Wide>(std::numeric_limits<std::int64_t>::max()))
    throw std::overflow_error("растровая площадь полигональной целевой функции находится вне диапазона");
  return static_cast<std::uint64_t>(std::llround(value));
}

/// Вычисляет точные первичные метрики состояния без построения растра.
PolygonObjectiveComponents measureExactObjective(const PolygonEnvironment & environment, const PolygonState & state)
{
  PolygonObjectiveComponents result;
  result.placedParts = state.placements.size();
  result.totalParts = environment.instances().size();
  result.placedArea = state.placedArea;
  for (const PolygonPartInstance & instance : environment.instances())
    result.totalPartArea += instance.area;
  std::int64_t right = environment.sheetMargin();
  for (const PolygonPlacement & placement : state.placements)
  {
    const PolygonRing64 outer = environment.placedOuter(placement);
    for (const PolygonPoint64 & point : outer)
      right = std::max(right, point.x);
  }
  const std::int64_t usableWidth = environment.sheetWidth() - 2 * environment.sheetMargin();
  const std::int64_t usableHeight = environment.sheetHeight() - 2 * environment.sheetMargin();
  result.usedLength = state.placements.empty() ? 0 : right - environment.sheetMargin();
  result.primaryRemnantWidth = usableWidth - result.usedLength;
  result.materialUtilization =
    usableWidth > 0 && usableHeight > 0
      ? static_cast<double>(state.placedArea) / (static_cast<double>(usableWidth) * static_cast<double>(usableHeight))
      : 0.0;
  return result;
}

/// Дополняет точные метрики устойчивыми вторичными растровыми компонентами.
void measureRasterObjective(const PolygonEnvironment & environment, const PolygonState & state,
                            PolygonObjectiveComponents & result)
{
  const RasterGeometry raster{environment.sheetWidth() - 2 * environment.sheetMargin(),
                              environment.sheetHeight() - 2 * environment.sheetMargin(),
                              usedRasterColumns(result.usedLength, environment.sheetWidth() - 2 * environment.sheetMargin())};
  const std::vector<unsigned char> freeCells = rasterizeFreeCells(environment, state, raster);
  const std::size_t largestRectangleCells = largestFreeRectangle(freeCells, raster.usedColumns);
  const auto [freeCount, largestComponent] = measureFreeComponents(freeCells, raster.usedColumns);
  const Wide cellArea =
    static_cast<Wide>(raster.usableWidth) * static_cast<Wide>(raster.usableHeight) / static_cast<Wide>(RASTER_CELL_COUNT);
  result.largestExtraRectangleArea = rasterArea(cellArea, largestRectangleCells);
  result.fragmentationPenalty = rasterArea(cellArea, freeCount - largestComponent);
}

} // namespace internal

using namespace internal;

/// Вычисляет основные метрики по точным границам, а вторичные — по стабильной сетке 128x128.
PolygonObjectiveComponents PolygonEnvironment::evaluate(const PolygonState & state) const
{
  if (state.objectiveCached)
    return state.cachedObjective;
  PolygonObjectiveComponents result = measureExactObjective(*this, state);
  measureRasterObjective(*this, state, result);
  state.cachedObjective = result;
  state.objectiveCached = true;
  return result;
}

} // namespace aipackaging::solver
