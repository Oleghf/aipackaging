#ifndef SCENEMOUSEEVENT_H__
#define SCENEMOUSEEVENT_H__

#include <event.h>
#include <point2d.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Кнопки мыши
/**
*/
////////////////////////////////////////////////////////////////////////////////
enum class MouseButton
{
  Left,
  Right,
  Middle
};


////////////////////////////////////////////////////////////////////////////////
//
/// Событие передвижения мыши по сцене
/**
*/
////////////////////////////////////////////////////////////////////////////////
class SceneMouseEvent : public Event
{
public:
  SceneMouseEvent(EventType type, const Point2D & scenePos, MouseButton button);

  EventType type() const override { return type_; }

  // Позиция на сцене
  Point2D scenePos() const { return scenePos_; }

  // Кнопка мыши
  MouseButton button() const { return button_; }

private:
  const EventType type_;
  const Point2D scenePos_;
  const MouseButton button_;
};

#endif
