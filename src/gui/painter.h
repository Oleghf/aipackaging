#ifndef QTPAINTER_H__
#define QTPAINTER_H__

#include <primitiveview.h>

class QPainter;

////////////////////////////////////////////////////////////////////////////////
//
/// Адаптер рисования
/**
*/
////////////////////////////////////////////////////////////////////////////////
class Painter : public PrimitiveView
{
public:
  Painter(QPainter & painter);

  void line(const Point2D & p1, const Point2D & p2) override;
  void circle(const Point2D & center, double radius) override;
  void rect(const Rect2D & rect) override;

  // Цвет обводки
  void setPenColor(const Color & color) override;

  // Цвет заливки
  void setBrushColor(const Color & color) override;

  // Толщина линий обводки
  void setThickness(double thickness) override;

  void scale(double factorx, double factory) override;

  void translate(const Vector2D & offset) override;

private:
  QPainter & painter_;
};

#endif
