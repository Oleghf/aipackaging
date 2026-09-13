#include <utility>
#include <vector>

#include <board.h>
#include <packing/previewpoolpresenter.h>

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
PreviewPoolPresenter::PreviewPoolPresenter(std::shared_ptr<Board> board)
  : board_(std::move(board))
{
}


//------------------------------------------------------------------------------
/**
  Синхронизирует фигуру предварительного просмотра с доской генерации
*/
//--
void PreviewPoolPresenter::showPreview(const std::shared_ptr<Figure> & figure) const
{
  clear();

  if (figure)
    board_->add(figure);
}


//------------------------------------------------------------------------------
/**
  Удаляет все фигуры с доски генерации
*/
//--
void PreviewPoolPresenter::clear() const
{
  const std::vector<std::shared_ptr<Figure>> figures = board_->getFigures();
  for (const std::shared_ptr<Figure> & figure : figures)
    board_->remove(figure);
}
