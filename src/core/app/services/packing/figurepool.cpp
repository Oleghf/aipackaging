#include <utility>

#include <packing/figurepool.h>

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
FigurePool::FigurePool()
  : randomEngine_(std::random_device{}())
{
}


//------------------------------------------------------------------------------
/**
  Конструктор с фиксированным seed генератора случайных чисел
*/
//--
FigurePool::FigurePool(unsigned int seed)
  : randomEngine_(seed)
{
}


//------------------------------------------------------------------------------
/**
  Загружает набор фигур в пул
*/
//--
void FigurePool::load(std::vector<std::shared_ptr<Figure>> figures)
{
  remaining_ = std::move(figures);
  preview_.reset();
  promotePreview();
}


//------------------------------------------------------------------------------
/**
  Очищает пул фигур
*/
//--
void FigurePool::clear()
{
  remaining_.clear();
  preview_.reset();
}


//------------------------------------------------------------------------------
/**
  Проверяет, пуст ли пул фигур
*/
//--
bool FigurePool::isEmpty() const
{
  return !preview_ && remaining_.empty();
}


//------------------------------------------------------------------------------
/**
  Проверяет наличие preview фигуры
*/
//--
bool FigurePool::hasPreview() const
{
  return static_cast<bool>(preview_);
}


//------------------------------------------------------------------------------
/**
  Возвращает текущую preview фигуру
*/
//--
std::shared_ptr<Figure> FigurePool::previewFigure() const
{
  return preview_;
}


//------------------------------------------------------------------------------
/**
  Забирает текущую preview фигуру и продвигает пул
*/
//--
std::shared_ptr<Figure> FigurePool::takePreview()
{
  std::shared_ptr<Figure> result = preview_;
  preview_.reset();
  promotePreview();
  return result;
}


//------------------------------------------------------------------------------
/**
  Возвращает фигуру в пул как текущую preview-фигуру
*/
//--
void FigurePool::returnAsPreview(std::shared_ptr<Figure> figure)
{
  if (!figure)
    return;

  if (preview_)
    remaining_.push_back(preview_);

  preview_ = std::move(figure);
}


//------------------------------------------------------------------------------
/**
  Возвращает snapshot текущего оставшегося пула
*/
//--
std::vector<std::shared_ptr<Figure>> FigurePool::snapshotRemaining() const
{
  std::vector<std::shared_ptr<Figure>> result;
  if (preview_)
    result.push_back(preview_);

  result.insert(result.end(), remaining_.begin(), remaining_.end());
  return result;
}


//------------------------------------------------------------------------------
/**
  Снимает snapshot внутреннего состояния пула
*/
//--
FigurePool::State FigurePool::state() const
{
  return {remaining_, preview_};
}


//------------------------------------------------------------------------------
/**
  Восстанавливает внутреннее состояние пула
*/
//--
void FigurePool::restore(const State & state)
{
  remaining_ = state.remaining;
  preview_ = state.preview;
}


//------------------------------------------------------------------------------
/**
  Делает следующую случайную фигуру preview фигурой
*/
//--
void FigurePool::promotePreview()
{
  if (remaining_.empty())
    return;

  std::uniform_int_distribution<size_t> indexDistribution(0, remaining_.size() - 1);
  const size_t previewIndex = indexDistribution(randomEngine_);
  preview_ = remaining_[previewIndex];
  remaining_.erase(remaining_.begin() + static_cast<std::ptrdiff_t>(previewIndex));
}
