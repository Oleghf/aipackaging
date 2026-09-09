#ifndef FIGURESCENEGEOMETRY_H__
#define FIGURESCENEGEOMETRY_H__

#include <algorithm>
#include <vector>

#include <algs.h>
#include <point2d.h>

namespace FigureSceneGeometry
{
inline std::vector<std::vector<Point2D>> cellPointGroups(const Figure & figure, size_t cellSize)
{
  std::vector<std::vector<Point2D>> cellPointGroups;
  cellPointGroups.reserve(figure.GetCells().size());

  for (const Cell2D & cell : figure.GetCells())
  {
    std::vector<Point2D> cellPoints = cell.GetPoints();
    std::vector<Point2D> cellScenePoints;
    cellScenePoints.reserve(cellPoints.size());

    std::transform(cellPoints.begin(), cellPoints.end(), std::back_inserter(cellScenePoints),
                   [&figure, cellSize, &cell](const Point2D & cellP)
                   {
                     double cellSceneX = figure.topLeft().x + cell.GetCoordinates().column * cellSize + cellP.x * cellSize;
                     double cellSceneY = figure.topLeft().y + cell.GetCoordinates().row * cellSize + cellP.y * cellSize;
                     return Point2D{cellSceneX, cellSceneY};
                   });

    cellPointGroups.push_back(std::move(cellScenePoints));
  }

  return cellPointGroups;
}
} // namespace FigureSceneGeometry

#endif
