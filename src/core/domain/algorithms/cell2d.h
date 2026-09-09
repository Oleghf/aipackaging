#ifndef CELL2D_H
#define CELL2D_H

#include <memory>
#include <vector>

#include <coordinates2d.h>
#include <countour2d.h>

struct Matrix3;
class BoundedCurve2D;
class Rect2D;

////////////////////////////////////////////////////////////////////////////////
//
/// Ячейка в двумерном пространстве
/**
*/
////////////////////////////////////////////////////////////////////////////////
class Cell2D
{
public:
  Cell2D(const Coordinates & positions, std::vector<std::unique_ptr<BoundedCurve2D>> boundaryCurves);

  // Заменяются на сдвиг и поворот.
  // Сеттеры и геттеры, признак плохого проектирования
  // Не нужно превращать класс в контейнер, вынося всю логику его поведения во вне класса
  void SetCoordinates(const Coordinates & positions);
  Coordinates GetCoordinates() const;

  Rect2D boundingBox() const;

  void Move(int dcolumn, int drow);

  std::vector<Point2D> GetPoints() const;

private:
  Coordinates positions_;
  std::unique_ptr<Countour2D> contour_;
};


////////////////////////////////////////////////////////////////////////////////
//
/// Создает квадратную ячейку
/**
*/
////////////////////////////////////////////////////////////////////////////////
Cell2D CreateSquareCell(const Coordinates & coords);

#endif
