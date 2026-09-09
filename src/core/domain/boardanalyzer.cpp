#include <cassert>

#include <board.h>
#include <boardanalyzer.h>
#include <rect2d.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
BoardAnalyzer::BoardAnalyzer(std::shared_ptr<Board> board)
  : board_(std::move(board))
  , occupancyGrid()
  , isNeedRecount(true)
{
  assert(board_.lock());

  for (size_t i = 0; i < board_.lock()->countColumns(); i++)
    occupancyGrid.push_back(std::vector<bool>(board_.lock()->countRows(), false));
}


//
std::shared_ptr<BoardAnalyzer> BoardAnalyzer::create(std::shared_ptr<Board> board)
{
  auto analyzer = std::shared_ptr<BoardAnalyzer>(new BoardAnalyzer(board));

  board->addEventListener(analyzer);

  return analyzer;
}


//
void BoardAnalyzer::onBoardEvent(const BoardEvent & event)
{
  isNeedRecount = true;
}


//------------------------------------------------------------------------------
/**
  Возвращает количество пустых клеток
*/
//--
size_t BoardAnalyzer::countEmptyCells()
{
  if (isNeedRecount)
  {
    updateOccupancyGrid();
    isNeedRecount = false;
  }

  size_t countEmptyCells = 0;

  for (auto & column : occupancyGrid)
  {
    for (bool occupancy : column)
    {
      if (!occupancy)
        countEmptyCells++;
    }
  }

  return countEmptyCells;
}


//------------------------------------------------------------------------------
/**
  Возвращает количество занятых клеток
*/
//--
size_t BoardAnalyzer::countOccupiedCells()
{
  if (isNeedRecount)
  {
    updateOccupancyGrid();
    isNeedRecount = false;
  }

  size_t countOccupiedCells = 0;

  for (auto & column : occupancyGrid)
  {
    for (bool occupancy : column)
    {
      if (occupancy)
        countOccupiedCells++;
    }
  }

  return countOccupiedCells;
}


//------------------------------------------------------------------------------
/**
  Полностью пересчитывает количество занятых клеток
*/
//--
void BoardAnalyzer::updateOccupancyGrid()
{
  if (board_.expired())
    return;
  auto boardShrd = board_.lock();

  occupancyGrid.clear();
  for (size_t i = 0; i < board_.lock()->countColumns(); i++)
    occupancyGrid.push_back(std::vector<bool>(boardShrd->countRows(), false));

  std::vector<std::shared_ptr<Figure>> figures = boardShrd->getFigures();

  for (auto & fig : figures)
  {
    for (auto & cell : fig->GetCells())
    {
      Coordinates coords = cell.GetCoordinates();
      if (coords.column < 0 || coords.row < 0)
        continue;

      if (static_cast<size_t>(coords.column) >= boardShrd->countColumns() ||
          static_cast<size_t>(coords.row) >= boardShrd->countRows())
        continue;

      occupancyGrid[coords.column][coords.row] = true;
    }
  }
}
