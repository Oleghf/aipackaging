#ifndef EVENTLISTENER_H__
#define EVENTLISTENER_H__

#include <memory>

#include <event.h>

class Event;

////////////////////////////////////////////////////////////////////////////////
//
/// Слушатель событий
/**
*/
////////////////////////////////////////////////////////////////////////////////
class EventListener
{
public:
  virtual ~EventListener() = default;

  virtual void onEvent(const Event & event) = 0;
};


#endif
