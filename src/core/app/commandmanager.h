#ifndef COMMANDMANAGER_H__
#define COMMANDMANAGER_H__

#include <deque>
#include <memory>

class ICommand;

////////////////////////////////////////////////////////////////////////////////
//
/// Менеджер команд
/**
*/
////////////////////////////////////////////////////////////////////////////////
class CommandManager
{
public:
  CommandManager(unsigned int stackSize = 100);
  ~CommandManager();

  // Инициализировать команду
  void execute(std::unique_ptr<ICommand> command);
  // Отменить команду
  void undo();
  // Повторить команду
  void redo();
  // Очистить историю команд
  void clear();

private:
  std::deque<std::unique_ptr<ICommand>> undoStack_;
  std::deque<std::unique_ptr<ICommand>> redoStack_;
  const unsigned int sizeStack_;
};

#endif
