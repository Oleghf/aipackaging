#ifndef MOVEREQUESTEVENT_H__
#define MOVEREQUESTEVENT_H__

#include <event.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Перечисление возможных действий
/**
*/
////////////////////////////////////////////////////////////////////////////////
enum class Action
{
  Up,
  Down,
  Right,
  Left,
  Rotate,
  Delete,
  AutoPlace,
  AutoPackAll
};

////////////////////////////////////////////////////////////////////////////////
//
/// Событие: пользователь хочет выполнить действие на сцене
/**
*/
////////////////////////////////////////////////////////////////////////////////
class ActionEvent : public Event
{
public:
  ActionEvent(Action action)
    : action_(action) {};

  EventType type() const { return EventType::Action; }

  // Возвращает действие, которое пользователь хочет совершить с фигурой
  Action action() const { return action_; }

private:
  const Action action_;
};

#endif
