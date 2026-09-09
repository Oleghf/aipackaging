#ifndef ROTATECOMMAND_H__
#define ROTATECOMMAND_H__

#include <memory>

#include <icommand.h>

class Figure;

////////////////////////////////////////////////////////////////////////////////
//
/// Команда поворота фигуры
/**
*/
////////////////////////////////////////////////////////////////////////////////
class RotateCommand : public ICommand
{
public:
  RotateCommand(std::shared_ptr<Figure> figure);

  void execute() override;
  void undo() override;

private:
  std::shared_ptr<Figure> figure_;
};

#endif
