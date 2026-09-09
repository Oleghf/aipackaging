#include <commandmanager.h>
#include <icommand.h>

//------------------------------------------------------------------------------
/**
  Конструктор, принимающий размер стека команд
*/
//--
CommandManager::CommandManager(unsigned int stackSize)
  : sizeStack_(stackSize)
{
}


//------------------------------------------------------------------------------
/**
  Деструктор
  \details Применен таким образом для корректного освобождения ресурсов у unique ptr
*/
//--
CommandManager::~CommandManager() = default;


//------------------------------------------------------------------------------
/**
  Исполняет переданную команду и добавляет ее в стек команд, которые можно отменить
*/
//--
void CommandManager::execute(std::unique_ptr<ICommand> command)
{
  command->execute();
  undoStack_.push_back(std::move(command));
  redoStack_.clear();
}


//------------------------------------------------------------------------------
/**
  Отменяет последнюю исполненную команду и добавляет ее в стек команд, которые можно повторить
*/
//--
void CommandManager::undo()
{
  if (undoStack_.empty())
    return;

  std::unique_ptr<ICommand> cmd = std::move(undoStack_.back());
  undoStack_.pop_back();
  cmd->undo();

  if (redoStack_.size() == sizeStack_)
    redoStack_.pop_front();

  redoStack_.push_back(std::move(cmd));
}


//------------------------------------------------------------------------------
/**
  Повторяет последнюю отменную команду и добавляет ее в стек команд, которые можно отменить
*/
//--
void CommandManager::redo()
{
  if (redoStack_.empty())
    return;

  std::unique_ptr<ICommand> cmd = std::move(redoStack_.back());
  redoStack_.pop_back();
  cmd->execute();

  if (undoStack_.size() == sizeStack_)
    undoStack_.pop_front();

  undoStack_.push_back(std::move(cmd));
}


//------------------------------------------------------------------------------
/**
  Очищает историю команд
*/
//--
void CommandManager::clear()
{
  undoStack_.clear();
  redoStack_.clear();
}
