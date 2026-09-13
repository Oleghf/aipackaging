#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <board.h>
#include <cell2d.h>
#include <deletefigurecommand.h>
#include <objectfigure.h>
#include <point2d.h>
#include <selectionmodel.h>

namespace
{
constexpr Point2D PREVIEW_TOP_LEFT{5, -5};

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
  Создаёт нормализованную копию удалённой фигуры для предварительного просмотра
	*/
//--
std::shared_ptr<Figure> makePreviewFigure(const std::shared_ptr<Figure> & sourceFigure)
{
  if (!sourceFigure || sourceFigure->GetCells().empty())
    return nullptr;

  int minColumn = std::numeric_limits<int>::max();
  int minRow = std::numeric_limits<int>::max();
  for (const Cell2D & cell : sourceFigure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    minColumn = std::min(minColumn, coords.column);
    minRow = std::min(minRow, coords.row);
  }

  std::vector<Cell2D> cells;
  cells.reserve(sourceFigure->GetCells().size());
  for (const Cell2D & cell : sourceFigure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    cells.push_back(CreateSquareCell({coords.column - minColumn, coords.row - minRow}));
  }

  return std::make_shared<ObjectFigure>(PREVIEW_TOP_LEFT, std::move(cells));
}
} // namespace


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
DeleteFigureCommand::DeleteFigureCommand(std::shared_ptr<Board> board, FigurePool & figurePool,
                                         PreviewPoolPresenter previewPresenter, std::shared_ptr<SelectionModel> selection,
                                         std::shared_ptr<Figure> * activeFigure)
  : board_(std::move(board))
  , figurePool_(figurePool)
  , previewPresenter_(std::move(previewPresenter))
  , selection_(std::move(selection))
  , activeFigure_(activeFigure)
{
}


//------------------------------------------------------------------------------
/**
  Удаляет активную фигуру и возвращает её в пул предварительного просмотра
*/
//--
void DeleteFigureCommand::execute()
{
  if (!hasAfterState_)
  {
    beforeState_ = figurePool_.state();
    activeFigureBefore_ = activeFigure_ ? *activeFigure_ : nullptr;
    deletedFigure_ = activeFigureBefore_;
    returnedPreviewFigure_ = makePreviewFigure(deletedFigure_);

    if (!deletedFigure_ || !returnedPreviewFigure_)
      return;

    board_->remove(deletedFigure_);
    if (activeFigure_)
      *activeFigure_ = nullptr;
    updateActiveSelection(selection_, nullptr);
    figurePool_.returnAsPreview(returnedPreviewFigure_);
    afterState_ = figurePool_.state();
    hasAfterState_ = true;
  }
  else
  {
    if (deletedFigure_)
      board_->remove(deletedFigure_);
    if (activeFigure_)
      *activeFigure_ = nullptr;
    updateActiveSelection(selection_, nullptr);
    figurePool_.restore(afterState_);
  }

  previewPresenter_.showPreview(figurePool_.previewFigure());
}


//------------------------------------------------------------------------------
/**
  Возвращает удаленную фигуру на доску и восстанавливает состояние пула
*/
//--
void DeleteFigureCommand::undo()
{
  if (!hasAfterState_)
    return;

  figurePool_.restore(beforeState_);
  if (deletedFigure_)
    board_->add(deletedFigure_);

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
