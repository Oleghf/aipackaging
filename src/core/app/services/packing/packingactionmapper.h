#ifndef PACKINGACTIONMAPPER_H__
#define PACKINGACTIONMAPPER_H__

#include <memory>

class Figure;
class ICommand;
enum class Action;

////////////////////////////////////////////////////////////////////////////////
//
/// Маппер действий пользователя в команды режима упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PackingActionMapper
{
public:
  // Построить команду по действию пользователя и активной фигуре
  std::unique_ptr<ICommand> commandFor(Action action, const std::shared_ptr<Figure> & activeFigure) const;
};

#endif
