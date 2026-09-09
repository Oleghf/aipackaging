#ifndef ISTATE_H__
#define ISTATE_H__

#include <memory>

#include <icommand.h>

class Event;

////////////////////////////////////////////////////////////////////////////////
//
/// Интерфейс состояния
/**
*/
////////////////////////////////////////////////////////////////////////////////
class IState
{
public:
  // Получает событие и возвращает команду, если она была создана
  virtual std::unique_ptr<ICommand> onEvent(const Event & event) = 0;
};


#endif
