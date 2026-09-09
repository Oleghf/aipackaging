#include <algs.h>
#include <board.h>
#include <createfigurecommand.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
CreateFigureCommand::CreateFigureCommand(std::shared_ptr<Board> board, std::shared_ptr<Figure> figure)
  : board_(std::move(board))
  , figure_(std::move(figure))
{
}


//------------------------------------------------------------------------------
/**
  Исполняет команду
*/
//--
void CreateFigureCommand::execute()
{
  board_->add(figure_);
}


//------------------------------------------------------------------------------
/**
  Отменяет команду
*/
//--
void CreateFigureCommand::undo()
{
  board_->remove(figure_);
}
