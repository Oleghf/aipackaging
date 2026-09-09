#include <cassert>
#include <utility>

#include <boundedcurve2d.h>
#include <cell2d.h>
#include <countour2d.h>
#include <line2d.h>
#include <point2d.h>
#include <rect2d.h>


//------------------------------------------------------------------------------
/**
  Конструктор. Принимает координаты доски(где будет располагаться ячейка) и кривые ячейки в координатах клетки(!)
*/
//--
Cell2D::Cell2D(const Coordinates & positions, std::vector<std::unique_ptr<BoundedCurve2D>> boundaryCurves)
  : positions_(positions)
  , contour_(Countour2D::Create(std::move(boundaryCurves)))
{
  assert(contour_);
}


//------------------------------------------------------------------------------
/**
  Устанавливает координаты на доске
*/
//--
void Cell2D::SetCoordinates(const Coordinates & positions)
{
  positions_.column = positions.column;
  positions_.row = positions.row;
}


//------------------------------------------------------------------------------
/**
  Возвращает координаты на доске
*/
//--
Coordinates Cell2D::GetCoordinates() const
{
  return positions_;
}


//
Rect2D Cell2D::boundingBox() const
{
  double minX = std::numeric_limits<double>::max();
  double maxX = std::numeric_limits<double>::lowest();
  double minY = std::numeric_limits<double>::max();
  double maxY = std::numeric_limits<double>::lowest();

  for (const Point2D & point : contour_->GetPoints())
  {
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y);
    maxY = std::max(maxY, point.y);
  }

  return {{minX, maxY}, {maxX, minY}};
}

//------------------------------------------------------------------------------
/**
  Двигает ячейку
*/
//--
void Cell2D::Move(int dcolumn, int drow)
{
  positions_.column += dcolumn;
  positions_.row += drow;
}


//------------------------------------------------------------------------------
/**
  Возвращает точки в координатах ячейки !!!
*/
//--
std::vector<Point2D> Cell2D::GetPoints() const
{
  return contour_->GetPoints();
}


//------------------------------------------------------------------------------
/**
  Создает квадратную ячейку
*/
//--
Cell2D CreateSquareCell(const Coordinates & coords)
{
  std::vector<std::unique_ptr<BoundedCurve2D>> curves;
  Rect2D rect({0, 0}, {1, 1}); // warning

  std::shared_ptr<Line2D> line1 = std::make_shared<Line2D>(rect.topLeft(), rect.bottomLeft() - rect.topLeft());
  std::unique_ptr<BoundedCurve2D> curve1 = std::make_unique<BoundedCurve2D>(line1, 0, 1);
  curves.push_back(std::move(curve1));

  std::shared_ptr<Line2D> line2 = std::make_shared<Line2D>(rect.bottomLeft(), rect.bottomRight() - rect.bottomLeft());
  std::unique_ptr<BoundedCurve2D> curve2 = std::make_unique<BoundedCurve2D>(line2, 0, 1);
  curves.push_back(std::move(curve2));

  std::shared_ptr<Line2D> line3 = std::make_shared<Line2D>(rect.bottomRight(), rect.topRight() - rect.bottomRight());
  std::unique_ptr<BoundedCurve2D> curve3 = std::make_unique<BoundedCurve2D>(line3, 0, 1);
  curves.push_back(std::move(curve3));

  std::shared_ptr<Line2D> line4 = std::make_shared<Line2D>(rect.topRight(), rect.topLeft() - rect.topRight());
  std::unique_ptr<BoundedCurve2D> curve4 = std::make_unique<BoundedCurve2D>(line4, 0, 1);
  curves.push_back(std::move(curve4));

  return Cell2D(coords, std::move(curves));
}
