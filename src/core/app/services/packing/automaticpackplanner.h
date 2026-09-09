#ifndef AUTOMATICPACKPLANNER_H__
#define AUTOMATICPACKPLANNER_H__

#include <memory>
#include <vector>

#include <packing/iautomaticplacementstrategy.h>
#include <packing/placementvalidator.h>

class Board;
class Figure;
class FigurePool;

struct AutomaticPackPlan
{
  std::vector<std::shared_ptr<Figure>> placements;
  PlacementValidationResult validation;

  bool success() const { return validation.canPlace(); }
};

////////////////////////////////////////////////////////////////////////////////
//
/// Строит план автоматической упаковки по принципу «всё или ничего» без мутации рабочей сцены.
/**
*/
////////////////////////////////////////////////////////////////////////////////
class AutomaticPackPlanner
{
public:
  AutomaticPackPlan createPlan(const std::shared_ptr<Board> & board, const FigurePool & figurePool,
                               const IAutomaticPlacementStrategy & strategy) const;

private:
  PlacementValidator validator_;
};

#endif
