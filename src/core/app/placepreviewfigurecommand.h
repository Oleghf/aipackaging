#ifndef PLACEPREVIEWFIGURECOMMAND_H__
#define PLACEPREVIEWFIGURECOMMAND_H__

#include <memory>

#include <icommand.h>
#include <packing/figurepool.h>
#include <packing/previewpoolpresenter.h>

class Board;
class Figure;
class SelectionModel;

enum class PreviewConsumptionPolicy
{
  PreservePreview,
  ConsumePreview
};

////////////////////////////////////////////////////////////////////////////////
//
/// Команда размещения фигуры предпросмотра из пула на доске упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PlacePreviewFigureCommand : public ICommand
{
public:
  // Конструктор
  PlacePreviewFigureCommand(std::shared_ptr<Board> board, FigurePool & figurePool, PreviewPoolPresenter previewPresenter,
                            std::shared_ptr<SelectionModel> selection, std::shared_ptr<Figure> * activeFigure,
                            std::shared_ptr<Figure> placedFigure = nullptr,
                            PreviewConsumptionPolicy previewConsumptionPolicy = PreviewConsumptionPolicy::PreservePreview);

  // Выполнить размещение фигуры предпросмотра
  void execute() override;
  // Отменить размещение фигуры предпросмотра
  void undo() override;

private:
  std::shared_ptr<Board> board_;
  FigurePool & figurePool_;
  PreviewPoolPresenter previewPresenter_;
  std::shared_ptr<SelectionModel> selection_;
  std::shared_ptr<Figure> * activeFigure_;
  std::shared_ptr<Figure> activeFigureBefore_;
  std::shared_ptr<Figure> sourceFigure_;
  std::shared_ptr<Figure> placedFigure_;
  PreviewConsumptionPolicy previewConsumptionPolicy_;
  FigurePool::State beforeState_;
  FigurePool::State afterState_;
  bool hasAfterState_ = false;
};

#endif
