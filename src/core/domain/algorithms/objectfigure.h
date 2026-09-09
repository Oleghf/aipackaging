#ifndef OBJECTFIGURE_H__
#define OBJECTFIGURE_H__

#include <utility>

#include <algs.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Фигура, загруженная из внешнего описания
/**
*/
////////////////////////////////////////////////////////////////////////////////
class ObjectFigure : public Figure
{
public:
  // Конструктор
  ObjectFigure(const Point2D & topLeft, std::vector<Cell2D> figureCells)
    : Figure(topLeft)
  {
    cells = std::move(figureCells);
  }
};

#endif
