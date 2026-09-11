#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <queue>

#include "polygoninternal.h"


namespace aipackaging::solver
{
namespace internal
{
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

} // namespace internal

using namespace internal;

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

} // namespace aipackaging::solver
