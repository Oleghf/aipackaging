#ifndef CREATEFIGURECOMMAND_H__
#define CREATEFIGURECOMMAND_H__

#include <memory>

#include <icommand.h>

class Figure;
class Board;

////////////////////////////////////////////////////////////////////////////////
//
/// Команда создания фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class CreateFigureCommand : public ICommand
{
public:
  CreateFigureCommand(std::shared_ptr<Board> board, std::shared_ptr<Figure> figure);

  void execute() override;
  void undo() override;

private:
  std::shared_ptr<Board> board_;
  std::shared_ptr<Figure> figure_;
};

#endif
