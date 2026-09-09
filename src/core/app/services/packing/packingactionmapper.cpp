#include <actionevent.h>
#include <algs.h>
#include <movecommand.h>
#include <packing/packingactionmapper.h>
#include <rotatecommand.h>

//------------------------------------------------------------------------------
/**
  Создает команду изменения активной фигуры по действию пользователя
*/
//--
std::unique_ptr<ICommand> PackingActionMapper::commandFor(Action action, const std::shared_ptr<Figure> & activeFigure) const
{
  if (!activeFigure)
    return nullptr;

  switch (action)
  {
    case Action::Up:
      return std::make_unique<MoveCommand>(activeFigure, FigureMove::UP);
    case Action::Down:
      return std::make_unique<MoveCommand>(activeFigure, FigureMove::DOWN);
    case Action::Left:
      return std::make_unique<MoveCommand>(activeFigure, FigureMove::LEFT);
    case Action::Right:
      return std::make_unique<MoveCommand>(activeFigure, FigureMove::RIGHT);
    case Action::Rotate:
      return std::make_unique<RotateCommand>(activeFigure);
    case Action::Delete:
      return nullptr;
    case Action::AutoPlace:
      return nullptr;
    case Action::AutoPackAll:
      return nullptr;
  }

  return nullptr;
}
