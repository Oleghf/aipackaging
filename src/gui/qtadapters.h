#ifndef QTADAPTERS_H__
#define QTADAPTERS_H__

#include <QColor>
#include <QKeyEvent>
#include <vector>

enum class MouseButton;
class Rect2D;
struct Point2D;
struct Color;

////////////////////////////////////////////////////////////////////////////////
//
/// Пространство адаптеров графического интерфейса
/**
*/
////////////////////////////////////////////////////////////////////////////////
namespace QtAdapters
{
Point2D fromQtPoint(const QPointF & point);
QPointF toQtPoint(const Point2D & point);

Rect2D fromQtRect(const QRectF & rect);
QRectF toQtRect(const Rect2D & rect);

MouseButton fromQtMouseButton(Qt::MouseButtons buttons);
Qt::MouseButtons toQtMouseButton(MouseButton button);

Color fromQtColor(QColor qcolor);
QColor toQtColor(Color color);
} // namespace QtAdapters

#endif
