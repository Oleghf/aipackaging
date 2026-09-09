#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <set>

#include <algs.h>
#include <board.h>
#include <figureevent.h>
#include <figuremovedevent.h>
#include <figurerotatedevent.h>
#include <ifigurelistener.h>
#include <mathutils.h>
#include <point2d.h>
#include <rect2d.h>


namespace
{
constexpr double CELL_SIZE = 100;
}


//
Rect2D Figure::boundingBox(double cellSize) const
{
  double minX = std::numeric_limits<double>::max();
  double maxX = std::numeric_limits<double>::lowest();
  double minY = std::numeric_limits<double>::max();
  double maxY = std::numeric_limits<double>::lowest();
  for (const Cell2D & cell : cells)
  {
    Rect2D cellBoundingBox = cell.boundingBox();
    minX = std::min(minX, cellBoundingBox.topLeft().x);
    maxX = std::max(maxX, cellBoundingBox.bottomRight().x);
    minY = std::min(minY, cellBoundingBox.bottomRight().y);
    maxY = std::max(maxY, cellBoundingBox.topLeft().y);
  }
  return Rect2D(Point2D{minX * cellSize, maxY * cellSize}, Point2D{maxX * cellSize, minY * cellSize});
}


//
Rect2D Figure::boundingBox() const
{
  std::vector<Point2D> allPoints = GetPoints(CELL_SIZE);

  double minX = std::numeric_limits<double>::max();
  double maxX = std::numeric_limits<double>::lowest();
  double minY = std::numeric_limits<double>::max();
  double maxY = std::numeric_limits<double>::lowest();

  for (const Point2D & point : allPoints)
  {
    minX = std::min(minX, point.x);
    maxX = std::max(maxX, point.x);
    minY = std::min(minY, point.y);
    maxY = std::max(maxY, point.y);
  }

  return Rect2D(Point2D{minX, maxY}, Point2D{maxX, minY});
}


void Figure::Move(FigureMove where)
{
  std::vector<Coordinates> from;
  from.reserve(cells.size());

  std::transform(cells.begin(), cells.end(), std::back_inserter(from), [](const Cell2D & cell) { return cell.GetCoordinates(); });

  switch (where)
  {
    case FigureMove::UP:
    {
      for (Cell2D & cell : cells)
        cell.Move(0, -1);
      if (hasRotationPivot_)
        rotationPivot_.row -= 1;
      for (Coordinates & coordinate : rotationBaseCoordinates_)
        coordinate.row -= 1;
      break;
    }
    case FigureMove::DOWN:
    {
      for (Cell2D & cell : cells)
        cell.Move(0, 1);
      if (hasRotationPivot_)
        rotationPivot_.row += 1;
      for (Coordinates & coordinate : rotationBaseCoordinates_)
        coordinate.row += 1;
      break;
    }
    case FigureMove::RIGHT:
    {
      for (Cell2D & cell : cells)
        cell.Move(1, 0);
      if (hasRotationPivot_)
        rotationPivot_.column += 1;
      for (Coordinates & coordinate : rotationBaseCoordinates_)
        coordinate.column += 1;
      break;
    }
    case FigureMove::LEFT:
    {
      for (Cell2D & cell : cells)
        cell.Move(-1, 0);
      if (hasRotationPivot_)
        rotationPivot_.column -= 1;
      for (Coordinates & coordinate : rotationBaseCoordinates_)
        coordinate.column -= 1;
      break;
    }
  }

  std::vector<Coordinates> to;
  to.reserve(cells.size());

  std::transform(cells.begin(), cells.end(), std::back_inserter(to), [](const Cell2D & cell) { return cell.GetCoordinates(); });

  notifyListeners(FigureMovedEvent(id_, from, to));
}


void Figure::addEventListener(std::shared_ptr<IFigureListener> listener)
{
  listeners_.push_back(std::move(listener));
}


void Figure::removeEventListener(std::shared_ptr<IFigureListener> listener)
{
  std::erase(listeners_, std::move(listener));
}


std::pair<double, double> Figure::FigureCenter(const std::vector<Coordinates> & coordinates)
{
  if (coordinates.empty())
    return {0.0, 0.0};

  auto sum = std::accumulate(coordinates.begin(), coordinates.end(), std::pair<double, double>{0.0, 0.0},
                             [](const std::pair<double, double> & acc, const Coordinates & coordinate)
                             { return std::pair<double, double>{acc.first + coordinate.column, acc.second + coordinate.row}; });

  return {sum.first / coordinates.size(), sum.second / coordinates.size()};
}


std::pair<int, int> Figure::CellRotate(int column, int row, const std::pair<int, int> & pivot)
{
  const int centeredColumn = column - pivot.first;
  const int centeredRow = row - pivot.second;

  const int rotatedColumn = centeredRow;
  const int rotatedRow = -centeredColumn;

  return {pivot.first + rotatedColumn, pivot.second + rotatedRow};
}


void Figure::Rotate()
{
  std::vector<Coordinates> from;
  from.reserve(cells.size());

  std::transform(cells.begin(), cells.end(), std::back_inserter(from), [](const Cell2D & cell) { return cell.GetCoordinates(); });

  if (!hasRotationPivot_)
  {
    const std::pair<double, double> center = FigureCenter(from);
    rotationPivot_ = {int(std::round(center.first)), int(std::round(center.second))};
    hasRotationPivot_ = true;
  }
  if (rotationBaseCoordinates_.empty())
    rotationBaseCoordinates_ = from;

  const std::pair<int, int> pivot{rotationPivot_.column, rotationPivot_.row};
  auto rotateCoordinates = [this, &pivot](const std::vector<Coordinates> & coordinates, size_t turns)
  {
    std::vector<Coordinates> rotatedCoordinates;
    rotatedCoordinates.reserve(coordinates.size());

    for (const Coordinates & coordinate : coordinates)
    {
      std::pair<int, int> rotated{coordinate.column, coordinate.row};
      for (size_t step = 0; step < turns; step++)
        rotated = CellRotate(rotated.first, rotated.second, pivot);

      rotatedCoordinates.push_back({rotated.first, rotated.second});
    }

    return rotatedCoordinates;
  };

  auto normalize = [](const std::vector<Coordinates> & coordinates)
  {
    std::multiset<std::pair<int, int>> normalized;
    for (const Coordinates & coordinate : coordinates)
      normalized.emplace(coordinate.column, coordinate.row);

    return normalized;
  };

  size_t currentState = 0;
  bool isMatched = false;
  const std::multiset<std::pair<int, int>> currentCoordinates = normalize(from);

  for (size_t candidate = 0; candidate < 4; candidate++)
  {
    if (normalize(rotateCoordinates(rotationBaseCoordinates_, candidate)) == currentCoordinates)
    {
      currentState = candidate;
      isMatched = true;
      break;
    }
  }

  if (!isMatched)
  {
    rotationBaseCoordinates_ = from;
    currentState = 0;
  }

  rotationState_ = (currentState + 1) % 4;
  std::vector<Coordinates> to = rotateCoordinates(rotationBaseCoordinates_, rotationState_);

  for (size_t i = 0; i < cells.size(); i++)
    cells[i].SetCoordinates(to[i]);

  notifyListeners(FigureRotatedEvent(id_));
}


//
bool Figure::Contains(const Point2D & point, double epsilon) const
{
  for (const std::vector<Point2D> & cellPoints : GetCellPointGroups(100))
  {
    for (size_t i = 1; i < cellPoints.size(); i += 2)
    {
      if (segmentIntersectsCircle(cellPoints[i - 1], cellPoints[i], point, epsilon))
        return true;
    }
  }
  return false;
}

//------------------------------------------------------------------------------
/**
  Вычисление реальных координат фигуры, возвращение точек на сцене
*/
//--
std::vector<std::vector<Point2D>> Figure::GetCellPointGroups(size_t cellSize) const
{
  std::vector<std::vector<Point2D>> cellPointGroups;
  cellPointGroups.reserve(cells.size());

  for (const Cell2D & cell : cells)
  {
    std::vector<Point2D> cellPoints = cell.GetPoints();
    std::vector<Point2D> cellScenePoints;
    cellScenePoints.reserve(cellPoints.size());

    std::transform(cellPoints.begin(), cellPoints.end(), std::back_inserter(cellScenePoints),
                   [this, cellSize, &cell](const Point2D & cellP)
                   {
                     double cellSceneX = topLeft_.x + cell.GetCoordinates().column * cellSize + cellP.x * cellSize;
                     double cellSceneY = topLeft_.y + cell.GetCoordinates().row * cellSize + cellP.y * cellSize;
                     return Point2D{cellSceneX, cellSceneY};
                   });

    cellPointGroups.push_back(std::move(cellScenePoints));
  }

  return cellPointGroups;
}


std::vector<Point2D> Figure::GetPoints(size_t cellSize) const
{
  std::vector<Point2D> cellAllScenePoints;

  for (const std::vector<Point2D> & cellPoints : GetCellPointGroups(cellSize))
    cellAllScenePoints.insert(cellAllScenePoints.end(), cellPoints.begin(), cellPoints.end());

  return cellAllScenePoints;
}


void Figure::notifyListeners(const FigureEvent & event)
{
  for (std::shared_ptr<IFigureListener> & listener : listeners_)
  {
    listener->onFigureEvent(event);
  }
}


//
bool StrictIntersect(double minX1, double maxX1, double minY1, double maxY1, double minX2, double maxX2, double minY2,
                     double maxY2)
{
  bool intersectX = (minX1 < maxX2) && (maxX1 > minX2);
  bool intersectY = (minY1 < maxY2) && (maxY1 > minY2);

  return intersectX && intersectY;
}


//
bool FigureIntersect(const Figure & firstFigure, const Figure & secondFigure)
{
  Rect2D firstFigureBoundingBox = firstFigure.boundingBox();
  Rect2D secondFigureBoundingBox = secondFigure.boundingBox();

  if (!firstFigureBoundingBox.intersects(secondFigureBoundingBox))
  {
    return false;
  }

  std::vector<Point2D> firstFigurePoints = firstFigure.GetPoints(CELL_SIZE);
  std::vector<Point2D> secondFigurePoints = secondFigure.GetPoints(CELL_SIZE);

  size_t firstFigureCells = firstFigure.GetCells().size();
  size_t secondFigureCells = secondFigure.GetCells().size();

  size_t firstFigurePointsPerCell = firstFigurePoints.size() / firstFigureCells;
  size_t secondFigurePointsPerCell = secondFigurePoints.size() / secondFigureCells;

  for (size_t i = 0; i < firstFigureCells; i++)
  {
    double minX1 = std::numeric_limits<double>::max();
    double maxX1 = std::numeric_limits<double>::lowest();
    double minY1 = std::numeric_limits<double>::max();
    double maxY1 = std::numeric_limits<double>::lowest();

    for (size_t p = 0; p < firstFigurePointsPerCell; p++)
    {
      const Point2D & pt = firstFigurePoints[i * firstFigurePointsPerCell + p];
      minX1 = std::min(minX1, pt.x);
      maxX1 = std::max(maxX1, pt.x);
      minY1 = std::min(minY1, pt.y);
      maxY1 = std::max(maxY1, pt.y);
    }

    for (size_t j = 0; j < secondFigureCells; j++)
    {
      double minX2 = std::numeric_limits<double>::max();
      double maxX2 = std::numeric_limits<double>::lowest();
      double minY2 = std::numeric_limits<double>::max();
      double maxY2 = std::numeric_limits<double>::lowest();

      for (size_t p = 0; p < secondFigurePointsPerCell; p++)
      {
        const Point2D & pt = secondFigurePoints[j * secondFigurePointsPerCell + p];
        minX2 = std::min(minX2, pt.x);
        maxX2 = std::max(maxX2, pt.x);
        minY2 = std::min(minY2, pt.y);
        maxY2 = std::max(maxY2, pt.y);
      }

      if (StrictIntersect(minX1, maxX1, minY1, maxY1, minX2, maxX2, minY2, maxY2))
      {
        return true;
      }
    }
  }

  return false;
}


//
bool BoundingBoxContains(const Board & board, const Figure & figure)
{
  Rect2D boardBox = board.boundingBox(CELL_SIZE);
  Rect2D figureBox = figure.boundingBox();

  if (boardBox.contains(figureBox))
  {
    return true;
  }

  return false;
}


/*

bool Intersect(Figure& f1, Figure& f2) {
	std::set<Coordinates> firstFigure;
	for (const Cell2D& cell : f1.GetCells()) {
		firstFigure.insert(cell.GetCoordinates());
	}

	std::set<Coordinates> secondFigure;
	for (const Cell2D& cell : f2.GetCells()) {
		secondFigure.insert(cell.GetCoordinates());
	}

	// ошибка
	/*
	std::set<Coordinates> intersection;

	std::set_intersection(firstFigure.begin(), firstFigure.end(), secondFigure.begin(), secondFigure.end(),
		std::inserter(intersection, intersection.begin()));


	return !intersection.empty();

	return false;
}









/*
bool IsOutOfBoard(const Figure& figure, int& width, int& height) {
	for (const Cell2D& cell : figure.cells) {
		int column = cell.GetCoordinates().x;
		int row = cell.GetCoordinates().y;
		if (column >= width || row >= height || column < 0 || row < 0) {
			return true;
		}
	}
	return false;
}*/
