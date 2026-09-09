#include <cmath>

#include <board.h>
#include <boardevent.h>
#include <boardfiguremovedevent.h>
#include <boardfigureplacedevent.h>
#include <boardfigureremovedevent.h>
#include <boardfigurerotatedevent.h>
#include <figuremovedevent.h>
#include <figurerotatedevent.h>
#include <generatorid.h>
#include <iboardlistener.h>
#include <primitiveview.h>
#include <rect2d.h>


std::shared_ptr<Board> Board::create(size_t countColumns, size_t countRows)
{
  return std::shared_ptr<Board>(new Board(countColumns, countRows));
}


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
Board::Board(size_t countColumns, size_t countRows)
  : id_(generateID())
  , countColumns_(countColumns)
  , countRows_(countRows)
{
}


//------------------------------------------------------------------------------
/**
  Добавить фигуру на доску
*/
//--
bool Board::add(std::shared_ptr<Figure> fig)
{
  if (!fig)
    return false;

  figures_.emplace_back(fig);
  fig->addEventListener(shared_from_this());
  notifyListeners(BoardFigurePlacedEvent(id_, fig->id()));
  return true;
}


//------------------------------------------------------------------------------
/**
  Удалить фигуру с доски
*/
//--
bool Board::remove(std::shared_ptr<Figure> fig)
{
  if (!fig)
    return false;

  std::erase(figures_, fig);
  fig->removeEventListener(shared_from_this());
  notifyListeners(BoardFigureRemovedEvent(id_, fig->id()));
  return true;
}


//------------------------------------------------------------------------------
/**
  Получить обрамляющий прямоугольник доски
*/
//--
Rect2D Board::boundingBox(double cellSize) const
{
  return Rect2D(Point2D{0, 0},
                Point2D{static_cast<double>(countColumns_) * cellSize, static_cast<double>(countRows_) * cellSize});
}


//------------------------------------------------------------------------------
/**
  Применить переданный предикат к каждой фигуре
*/
//--
void Board::forEachFigures(const std::function<bool(std::shared_ptr<Figure>)> & pred)
{
  for (std::shared_ptr<Figure> & figure : figures_)
  {
    if (!pred(figure))
      break;
  }
}


//------------------------------------------------------------------------------
/**
  Выполнить линейный поиск фигуры, в которую входит точка, с определенной погрешностью
*/
//--
std::shared_ptr<Figure> Board::findFigure(const Point2D & pos, double epsilon)
{
  auto it = std::ranges::find_if(figures_, [&pos, epsilon](const std::shared_ptr<Figure> & figure)
                                 { return figure->Contains(pos, epsilon); });

  if (it != figures_.end())
    return *it;
  return nullptr;
}


//
std::vector<Line2D> Board::getPrimitivesGrid(double cellSize) const
{
  std::vector<Line2D> result;

  Rect2D rect = boundingBox(cellSize);

  double startX = std::floor(rect.left() / cellSize) * cellSize;
  for (double x = startX; x <= rect.right(); x += cellSize)
  {
    Point2D p1{x, rect.top()};
    Point2D p2{x, rect.bottom()};
    result.push_back(Line2D(p1, {p2 - p1}));
  }

  double startY = std::floor(rect.bottom() / cellSize) * cellSize;
  for (double y = startY; y <= rect.top(); y += cellSize)
  {
    Point2D p1{rect.left(), y};
    Point2D p2{rect.right(), y};
    result.push_back(Line2D(p1, {p2 - p1}));
  }

  return result;
}


//
std::vector<std::shared_ptr<Figure>> Board::getFigures(const std::function<bool(std::shared_ptr<Figure>)> & filter) const
{
  std::vector<std::shared_ptr<Figure>> result;

  for (const std::shared_ptr<Figure> & fig : figures_)
  {
    if (filter(fig))
      result.push_back(fig);
  }
  return result;
}


//
void Board::addEventListener(std::shared_ptr<IBoardListener> listener)
{
  listeners_.push_back(std::move(listener));
}


//
void Board::removeEventListener(std::shared_ptr<IBoardListener> listener)
{
  std::erase(listeners_, std::move(listener));
}


//
void Board::onFigureEvent(const FigureEvent & event)
{
  switch (event.type())
  {
    case FigureEventType::FigureMoved:
    {
      const FigureMovedEvent & ev = static_cast<const FigureMovedEvent &>(event);
      notifyListeners(BoardFigureMovedEvent(id_, ev.figureID(), ev.from(), ev.to()));
      break;
    }
    case FigureEventType::FigureRotated:
    {
      const FigureRotatedEvent & ev = static_cast<const FigureRotatedEvent &>(event);
      notifyListeners(BoardFigureRotatedEvent(id_, ev.figureID()));
      break;
    }
  }
}


//
bool Board::operator==(const Board & rhs)
{
  return id_ == rhs.id_;
}


//
bool Board::operator!=(const Board & rhs)
{
  return !(*this == rhs);
}


//
void Board::notifyListeners(const BoardEvent & event)
{
  for (auto & listener : listeners_)
  {
    listener->onBoardEvent(event);
  }
}
