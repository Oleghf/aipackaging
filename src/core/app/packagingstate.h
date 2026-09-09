#ifndef PACKAGINGSTATE_H__
#define PACKAGINGSTATE_H__

#include <istate.h>

class Board;
class Event;
class IStatisticsView;
class PackingController;
class SelectionModel;

////////////////////////////////////////////////////////////////////////////////
//
/// Состояние упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PackagingState : public IState
{
public:
  // Конструктор
  PackagingState(std::shared_ptr<PackingController> controller);

  // Обработать событие состояния
  std::unique_ptr<ICommand> onEvent(const Event & event) override;

private:
  std::shared_ptr<PackingController> controller_;
};

#endif
