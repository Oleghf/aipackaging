#ifndef DELETEFIGURECOMMAND_H__
#define DELETEFIGURECOMMAND_H__

#include <memory>

#include <icommand.h>
#include <packing/figurepool.h>
#include <packing/previewpoolpresenter.h>

class Board;
class Figure;
class SelectionModel;

////////////////////////////////////////////////////////////////////////////////
//
/// Команда удаления активной фигуры со сцены упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class DeleteFigureCommand : public ICommand
{
public:
  // Конструктор
  DeleteFigureCommand(std::shared_ptr<Board> board, FigurePool & figurePool, PreviewPoolPresenter previewPresenter,
                      std::shared_ptr<SelectionModel> selection, std::shared_ptr<Figure> * activeFigure);

  // Удалить активную фигуру
  void execute() override;
  // Восстановить удаленную фигуру
  void undo() override;

private:
  std::shared_ptr<Board> board_;
  FigurePool & figurePool_;
  PreviewPoolPresenter previewPresenter_;
  std::shared_ptr<SelectionModel> selection_;
  std::shared_ptr<Figure> * activeFigure_;
  std::shared_ptr<Figure> activeFigureBefore_;
  std::shared_ptr<Figure> deletedFigure_;
  std::shared_ptr<Figure> returnedPreviewFigure_;
  FigurePool::State beforeState_;
  FigurePool::State afterState_;
  bool hasAfterState_ = false;
};

#endif
