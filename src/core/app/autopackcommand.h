#ifndef AUTOPACKCOMMAND_H__
#define AUTOPACKCOMMAND_H__

#include <cstddef>
#include <memory>
#include <vector>

#include <icommand.h>

class PlacePreviewFigureCommand;

////////////////////////////////////////////////////////////////////////////////
//
/// Составная команда для размещения серии фигур предпросмотра одним отменяемым действием.
/**
*/
////////////////////////////////////////////////////////////////////////////////
class AutoPackCommand : public ICommand
{
public:
  explicit AutoPackCommand(std::vector<std::unique_ptr<PlacePreviewFigureCommand>> commands);
  ~AutoPackCommand() override;

  void execute() override;
  void undo() override;

  size_t placedCount() const;

private:
  std::vector<std::unique_ptr<PlacePreviewFigureCommand>> commands_;
};

#endif
