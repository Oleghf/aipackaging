#include <utility>

#include <autopackcommand.h>
#include <placepreviewfigurecommand.h>

AutoPackCommand::AutoPackCommand(std::vector<std::unique_ptr<PlacePreviewFigureCommand>> commands)
  : commands_(std::move(commands))
{
}


AutoPackCommand::~AutoPackCommand() = default;


void AutoPackCommand::execute()
{
  for (const std::unique_ptr<PlacePreviewFigureCommand> & command : commands_)
    command->execute();
}


void AutoPackCommand::undo()
{
  for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
    (*it)->undo();
}


size_t AutoPackCommand::placedCount() const
{
  return commands_.size();
}
