#ifndef CHANGESTATEEVENT_H__
#define CHANGESTATEEVENT_H__

#include <event.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие изменения режима программы
/**
*/
////////////////////////////////////////////////////////////////////////////////
class ChangeStateEvent : public Event
{
public:
  EventType type() const override { return EventType::ChangeState; }
};

#endif
