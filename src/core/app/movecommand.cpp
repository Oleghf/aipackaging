#include <movecommand.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
MoveCommand::MoveCommand(std::shared_ptr<Figure> figure, FigureMove where)
  : figure_(std::move(figure))
  , where_(where)
{
}


//------------------------------------------------------------------------------
/**
  Исполняет команду
*/
//--
void MoveCommand::execute()
{
  figure_->Move(where_);
}


//------------------------------------------------------------------------------
/**
  Отменяет команду
*/
//--
void MoveCommand::undo()
{
  switch (where_)
  {
    case (FigureMove::UP):
      figure_->Move(FigureMove::DOWN);
      break;
    case (FigureMove::DOWN):
      figure_->Move(FigureMove::UP);
      break;
    case (FigureMove::LEFT):
      figure_->Move(FigureMove::RIGHT);
      break;
    case (FigureMove::RIGHT):
      figure_->Move(FigureMove::LEFT);
      break;
  }
}
