#include <utility>
#include <vector>

#include <board.h>
#include <cell2d.h>
#include <objectfigure.h>
#include <placepreviewfigurecommand.h>
#include <point2d.h>
#include <selectionmodel.h>

namespace
{
constexpr Point2D MAIN_BOARD_TOP_LEFT{0, 0};

//------------------------------------------------------------------------------
/**
	  Синхронизирует визуальное выделение с активной фигурой
	*/
//--
void updateActiveSelection(const std::shared_ptr<SelectionModel> & selection, const std::shared_ptr<Figure> & activeFigure)
{
  selection->clearFigures();
  if (activeFigure)
    selection->add(activeFigure);
}

//------------------------------------------------------------------------------
/**
	  Создает копию preview-фигуры для основной доски
	*/
//--
std::shared_ptr<Figure> makeMainBoardFigure(const std::shared_ptr<Figure> & sourceFigure)
{
  if (!sourceFigure)
    return nullptr;

  std::vector<Cell2D> cells;
  cells.reserve(sourceFigure->GetCells().size());

  for (const Cell2D & cell : sourceFigure->GetCells())
    cells.push_back(CreateSquareCell(cell.GetCoordinates()));

  return std::make_shared<ObjectFigure>(MAIN_BOARD_TOP_LEFT, std::move(cells));
}
} // namespace

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
PlacePreviewFigureCommand::PlacePreviewFigureCommand(std::shared_ptr<Board> board, FigurePool & figurePool,
                                                     PreviewPoolPresenter previewPresenter,
                                                     std::shared_ptr<SelectionModel> selection,
                                                     std::shared_ptr<Figure> * activeFigure, std::shared_ptr<Figure> placedFigure,
                                                     PreviewConsumptionPolicy previewConsumptionPolicy)
  : board_(std::move(board))
  , figurePool_(figurePool)
  , previewPresenter_(std::move(previewPresenter))
  , selection_(std::move(selection))
  , activeFigure_(activeFigure)
  , placedFigure_(std::move(placedFigure))
  , previewConsumptionPolicy_(previewConsumptionPolicy)
{
}


//------------------------------------------------------------------------------
/**
  Забирает preview из пула и размещает фигуру на основной доске
*/
//--
void PlacePreviewFigureCommand::execute()
{
  if (!hasAfterState_)
  {
    beforeState_ = figurePool_.state();
    activeFigureBefore_ = activeFigure_ ? *activeFigure_ : nullptr;
    if (!placedFigure_)
    {
      // Ручное размещение берёт текущую фигуру предпросмотра и создаёт копию для основной доски без смещения.
      sourceFigure_ = figurePool_.takePreview();

      if (!sourceFigure_)
        return;

      placedFigure_ = makeMainBoardFigure(sourceFigure_);
      if (!placedFigure_)
        return;
    }
    else if (previewConsumptionPolicy_ == PreviewConsumptionPolicy::ConsumePreview && figurePool_.hasPreview())
    {
      // Готовое автоматическое размещение явно продвигает пул только через политику потребления предпросмотра.
      sourceFigure_ = figurePool_.takePreview();
    }

    afterState_ = figurePool_.state();
    hasAfterState_ = true;
  }
  else
  {
    figurePool_.restore(afterState_);
  }

  board_->add(placedFigure_);
  if (activeFigure_)
  {
    if (figurePool_.hasPreview())
      *activeFigure_ = placedFigure_;
    else
      *activeFigure_ = nullptr;

    updateActiveSelection(selection_, *activeFigure_);
  }
  else
  {
    updateActiveSelection(selection_, nullptr);
  }

  previewPresenter_.showPreview(figurePool_.previewFigure());
}


//------------------------------------------------------------------------------
/**
  Удаляет размещенную фигуру и восстанавливает состояние пула
*/
//--
void PlacePreviewFigureCommand::undo()
{
  if (placedFigure_)
  {
    board_->remove(placedFigure_);
    selection_->remove(placedFigure_);
  }

  figurePool_.restore(beforeState_);
  if (activeFigure_)
  {
    *activeFigure_ = activeFigureBefore_;
    updateActiveSelection(selection_, *activeFigure_);
  }
  else
  {
    updateActiveSelection(selection_, nullptr);
  }

  previewPresenter_.showPreview(figurePool_.previewFigure());
}
