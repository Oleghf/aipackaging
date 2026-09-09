#ifndef ___ALG_H___
#define ___ALG_H___

#include <iostream>
#include <memory>
#include <vector>

#include <cell2d.h>
#include <generatorid.h>
#include <point2d.h>

class IFigureListener;
class FigureEvent;
class Board;
struct Point2D;

enum class FigureMove
{
  UP,
  DOWN,
  RIGHT,
  LEFT
};

class Figure
{
public:
  Figure(const Point2D & topLeft)
    : id_(generateID())
    , topLeft_(topLeft)
  {
  }

  inline size_t id() const { return id_; }
  inline const Point2D & topLeft() const { return topLeft_; }

  Rect2D boundingBox(double cellSize) const;

  Rect2D boundingBox() const;

  // Перемещение фигуры
  void Move(FigureMove where);
  void Rotate();
  bool Contains(const Point2D & point, double epsilon) const;

  std::vector<Point2D> GetPoints(size_t cellSize) const;
  std::vector<std::vector<Point2D>> GetCellPointGroups(size_t cellSize) const;
  const std::vector<Cell2D> & GetCells() const { return cells; };

  void addEventListener(std::shared_ptr<IFigureListener> listener);
  void removeEventListener(std::shared_ptr<IFigureListener> listener);

private:
  // Поворот фигуры
  static std::pair<double, double> FigureCenter(const std::vector<Coordinates> & coordinates);
  static std::pair<int, int> CellRotate(int column, int row, const std::pair<int, int> & pivot);

  void notifyListeners(const FigureEvent & event);

private:
  const size_t id_;
  std::vector<std::shared_ptr<IFigureListener>> listeners_;
  Point2D topLeft_;
  Coordinates rotationPivot_{0, 0};
  bool hasRotationPivot_ = false;
  std::vector<Coordinates> rotationBaseCoordinates_;
  size_t rotationState_ = 0;

protected:
  std::vector<Cell2D> cells;
};

// Строгая проверка пересечения прямоугольников
bool StrictIntersect(double minX1, double maxX1, double minY1, double maxY1, double minX2, double maxX2, double minY2,
                     double maxY2);

// Проверка на пересечение ограничивающих контуров двух фигур
bool FigureIntersect(const Figure & firstFigure, const Figure & secondFigure);

// Проверка на пересечение ограничивающего контура фигуры с контуром доски
bool BoundingBoxContains(const Board & board, const Figure & figure);


// -------------------------------
// КЛАССЫ НАСЛЕДНИКИ - ФОРМЫ ФИГУР

class IShape : public Figure
{
public:
  IShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row}));
    cells.push_back(CreateSquareCell({column, row + 1}));
    cells.push_back(CreateSquareCell({column, row + 2}));
    cells.push_back(CreateSquareCell({column, row + 3}));
  }
};


class JShape : public Figure
{
public:
  JShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column + 1, row}));
    cells.push_back(CreateSquareCell({column + 1, row + 1}));
    cells.push_back(CreateSquareCell({column + 1, row + 2}));
    cells.push_back(CreateSquareCell({column, row + 2}));
  }
};


class LShape : public Figure
{
public:
  LShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row}));
    cells.push_back(CreateSquareCell({column, row + 1}));
    cells.push_back(CreateSquareCell({column, row + 2}));
    cells.push_back(CreateSquareCell({column + 1, row + 2}));
  }
};


class OShape : public Figure
{
public:
  OShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row}));
    cells.push_back(CreateSquareCell({column + 1, row}));
    cells.push_back(CreateSquareCell({column, row + 1}));
    cells.push_back(CreateSquareCell({column + 1, row + 1}));
  }
};


class SShape : public Figure
{
public:
  SShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row + 1}));
    cells.push_back(CreateSquareCell({column + 1, row + 1}));
    cells.push_back(CreateSquareCell({column + 1, row}));
    cells.push_back(CreateSquareCell({column + 2, row}));
  }
};


class ZShape : public Figure
{
public:
  ZShape(const Point2D & topLeft, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row}));
    cells.push_back(CreateSquareCell({column + 1, row}));
    cells.push_back(CreateSquareCell({column + 1, row + 1}));
    cells.push_back(CreateSquareCell({column + 2, row + 1}));
  }
};


class TShape : public Figure
{
public:
  TShape(const Point2D & topLeft, int orientation, int column = 1, int row = 1)
    : Figure(topLeft)
  {
    cells.push_back(CreateSquareCell({column, row}));
    cells.push_back(CreateSquareCell({column + 1, row}));
    cells.push_back(CreateSquareCell({column + 2, row}));
    cells.push_back(CreateSquareCell({column + 1, row + 1}));
  }
};

#endif
