#ifndef BOARDANALYZER_H__
#define BOARDANALYZER_H__

#include <memory>
#include <vector>

#include <iboardlistener.h>

class Board;

////////////////////////////////////////////////////////////////////////////////
//
/// Анализатор доски
/**
*/
////////////////////////////////////////////////////////////////////////////////
class BoardAnalyzer : public std::enable_shared_from_this<BoardAnalyzer>,
                      public IBoardListener
{
public:
  static std::shared_ptr<BoardAnalyzer> create(std::shared_ptr<Board> board);

  void onBoardEvent(const BoardEvent & event) override;

  size_t countEmptyCells();
  size_t countOccupiedCells();

private:
  BoardAnalyzer(std::shared_ptr<Board> board);

  void updateOccupancyGrid();

private:
  const std::weak_ptr<Board> board_;
  std::vector<std::vector<bool>> occupancyGrid;

  bool isNeedRecount;
};

#endif
