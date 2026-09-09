#ifndef OPENSCENEFILEEVENT_H__
#define OPENSCENEFILEEVENT_H__

#include <event.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие инициации открытия файла сцены упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class OpenSceneFileEvent : public Event
{
public:
  EventType type() const override { return EventType::OpenScene; }
};

#endif
