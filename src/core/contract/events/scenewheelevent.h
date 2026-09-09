#ifndef SCENEWHEELEVENT_H__
#define SCENEWHEELEVENT_H__

#include <event.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Событие движения колесика мыши
/**
*/
////////////////////////////////////////////////////////////////////////////////
class SceneWheelEvent : public Event
{
public:
  SceneWheelEvent(double degrees)
    : degrees_(degrees)
  {
  }

  EventType type() const override { return EventType::Wheel; }

  double degrees() const { return degrees_; }

private:
  double degrees_;
};

#endif
