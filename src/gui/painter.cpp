#include <QPainter>

#include <color.h>
#include <painter.h>
#include <point2d.h>
#include <qtadapters.h>
#include <rect2d.h>
#include <vector2d.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
Painter::Painter(QPainter & painter)
  : painter_(painter)
{
}


//------------------------------------------------------------------------------
/**
  Рисует отрезок
*/
//--
void Painter::line(const Point2D & p1, const Point2D & p2)
{
  painter_.drawLine(p1.x, p1.y, p2.x, p2.y);
}


//------------------------------------------------------------------------------
/**
  Рисует окружность
*/
//--
void Painter::circle(const Point2D & center, double radius)
{
  painter_.drawEllipse({center.x, center.y}, radius, radius);
}


//------------------------------------------------------------------------------
/**
  Рисует прямоугольник
*/
//--
void Painter::rect(const Rect2D & rect)
{
  painter_.drawRect(QtAdapters::toQtRect(rect));
}


//------------------------------------------------------------------------------
/**
  Устанавливает цвет обводки
*/
//--
void Painter::setPenColor(const Color & color)
{
  QPen pen = painter_.pen();
  pen.setColor(QtAdapters::toQtColor(color));
  painter_.setPen(pen);
}


//------------------------------------------------------------------------------
/**
  Устанавливает цвет заливки
*/
//--
void Painter::setBrushColor(const Color & color)
{
  QBrush brush = painter_.brush();
  brush.setColor(QtAdapters::toQtColor(color));
  painter_.setBrush(brush);
}


//------------------------------------------------------------------------------
/**
  Устанавливает толщину линий обводки
*/
//--
void Painter::setThickness(double thickness)
{
  QPen pen = painter_.pen();
  pen.setWidthF(thickness);
  painter_.setPen(pen);
}


void Painter::scale(double factorx, double factory)
{
  painter_.scale(factorx, factory);
}


void Painter::translate(const Vector2D & offset)
{
  painter_.translate(offset.x, offset.y);
}
