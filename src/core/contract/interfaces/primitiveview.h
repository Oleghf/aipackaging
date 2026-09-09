#ifndef PRIMITIVEVIEW_H__
#define PRIMITIVEVIEW_H__

struct Point2D;
struct Vector2D;
class Rect2D;
struct Color;

////////////////////////////////////////////////////////////////////////////////
//
/// Интерфейс визуализатора примитивов
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PrimitiveView
{
public:
  // Отрисовать линию
  virtual void line(const Point2D & p1, const Point2D & p2) = 0;
  // Отрисовать окружность
  virtual void circle(const Point2D & center, double radius) = 0;
  // Отрисовать прямоугольник
  virtual void rect(const Rect2D & rect) = 0;

  // Цвет обводки
  virtual void setPenColor(const Color & color) = 0;

  // Цвет заливки
  virtual void setBrushColor(const Color & color) = 0;

  // Толщина линий обводки
  virtual void setThickness(double thickness) = 0;

  virtual void scale(double factorx, double factory) = 0;
  virtual void translate(const Vector2D & offset) = 0;
};


#endif
