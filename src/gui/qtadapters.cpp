#include <unordered_map>

#include <color.h>
#include <point2d.h>
#include <qtadapters.h>
#include <rect2d.h>
#include <scenemouseevent.h>


//------------------------------------------------------------------------------
/**
  Переводит qt точку в point2D
*/
//--
Point2D QtAdapters::fromQtPoint(const QPointF & qtPoint)
{
  return {qtPoint.x(), qtPoint.y()};
}


//------------------------------------------------------------------------------
/**
  Переводит point2d в qt точку
*/
//--
QPointF QtAdapters::toQtPoint(const Point2D & point)
{
  return QPointF(point.x, point.y);
}


//------------------------------------------------------------------------------
/**
  Переводит qrect в rect2d
*/
//--
Rect2D QtAdapters::fromQtRect(const QRectF & qtRect)
{
  return {fromQtPoint(qtRect.topLeft()), fromQtPoint(qtRect.bottomRight())};
}


//------------------------------------------------------------------------------
/**
  Переводит rect2d в qrectf
*/
//--
QRectF QtAdapters::toQtRect(const Rect2D & rect)
{
  return {toQtPoint(rect.topLeft()), toQtPoint(rect.bottomRight())};
}


//------------------------------------------------------------------------------
/**
  Переводит qmousebuttons в mousebutton
*/
//--
MouseButton QtAdapters::fromQtMouseButton(Qt::MouseButtons buttons)
{
  if (buttons.testFlag(Qt::MouseButton::LeftButton))
    return MouseButton::Left;
  else if (buttons.testFlag(Qt::MouseButton::RightButton))
    return MouseButton::Right;
  else
    return MouseButton::Middle;
}


//------------------------------------------------------------------------------
/**
  Переводит mousebutton в qtmousebutton
*/
//--
Qt::MouseButtons QtAdapters::toQtMouseButton(MouseButton button)
{
  switch (button)
  {
    case MouseButton::Left:
      return Qt::MouseButtons(Qt::LeftButton);
    case MouseButton::Right:
      return Qt::MouseButtons(Qt::RightButton);
    default:
      return Qt::MouseButtons(Qt::MiddleButton);
  }
}


//
Color QtAdapters::fromQtColor(QColor qcolor)
{
  return Color(qcolor.red(), qcolor.green(), qcolor.blue(), qcolor.alpha());
}


QColor QtAdapters::toQtColor(Color color)
{
  return QColor(color.r, color.g, color.b, color.a);
}
