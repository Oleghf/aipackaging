#ifndef PACKAGINGSTATISTICSSERVICE_H__
#define PACKAGINGSTATISTICSSERVICE_H__

#include <memory>

#include <boardanalyzer.h>
#include <iview.h>

namespace PackagingStatisticsService
{
inline void update(std::shared_ptr<BoardAnalyzer> boardAnalyzer, std::shared_ptr<IStatisticsView> view)
{
  size_t emptyCells = boardAnalyzer->countEmptyCells();
  size_t occupiedCells = boardAnalyzer->countOccupiedCells();

  view->statisticChangeCountAllCells(emptyCells + occupiedCells);
  view->statisticChangeCountOccupiedCells(occupiedCells);
}
} // namespace PackagingStatisticsService

#endif
