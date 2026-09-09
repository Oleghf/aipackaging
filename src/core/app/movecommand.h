#ifndef MOVECOMMAND_H__
#define MOVECOMMAND_H__

#include <memory>

#include <algs.h>
#include <icommand.h>


////////////////////////////////////////////////////////////////////////////////
//
/// Команда передвижения фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class MoveCommand : public ICommand
{
public:
  MoveCommand(std::shared_ptr<Figure> figure, FigureMove where);

  void execute() override;
  void undo() override;

private:
  std::shared_ptr<Figure> figure_;
  const FigureMove where_;
};

#endif
