#include <packagingstate.h>
#include <packingcontroller.h>

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
PackagingState::PackagingState(std::shared_ptr<PackingController> controller)
  : controller_(std::move(controller))
{
}


//------------------------------------------------------------------------------
/**
  Делегирует обработку события контроллеру упаковки
*/
//--
std::unique_ptr<ICommand> PackagingState::onEvent(const Event & event)
{
  return controller_->onEvent(event);
}
