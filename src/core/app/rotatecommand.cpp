#include <algs.h>
#include <rotatecommand.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
RotateCommand::RotateCommand(std::shared_ptr<Figure> figure)
  : figure_(figure)
{
}


//------------------------------------------------------------------------------
/**
  Исполняет команду
*/
//--
void RotateCommand::execute()
{
  figure_->Rotate();
}


//------------------------------------------------------------------------------
/**
  Отменяет команду
*/
//--
void RotateCommand::undo()
{
  figure_->Rotate();
  figure_->Rotate();
  figure_->Rotate();
}
