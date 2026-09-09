#include <utility>
#include <vector>

#include <board.h>
#include <cell2d.h>
#include <objectfigure.h>
#include <packing/automaticpackplanner.h>
#include <packing/figurepool.h>
#include <point2d.h>

namespace
{
constexpr Point2D MAIN_BOARD_TOP_LEFT{0, 0};

std::shared_ptr<Figure> cloneFigureForPlanning(const std::shared_ptr<Figure> & figure)
{
  if (!figure)
    return nullptr;

  std::vector<Cell2D> cells;
  cells.reserve(figure->GetCells().size());
  for (const Cell2D & cell : figure->GetCells())
    cells.push_back(CreateSquareCell(cell.GetCoordinates()));

  return std::make_shared<ObjectFigure>(MAIN_BOARD_TOP_LEFT, std::move(cells));
}
} // namespace


AutomaticPackPlan AutomaticPackPlanner::createPlan(const std::shared_ptr<Board> & board, const FigurePool & figurePool,
                                                   const IAutomaticPlacementStrategy & strategy) const
{
  if (!board)
    return {{}, {PlacementValidationCode::InvalidBoard}};

  for (const std::shared_ptr<Figure> & figure : board->getFigures())
  {
    const PlacementValidationResult validation = validator_.validate(board, figure);
    if (!validation.canPlace())
      return {{}, validation};
  }

  if (!figurePool.hasPreview())
    return {{}, {PlacementValidationCode::EmptyPool}};

  // План строится на временной доске и копии пула, чтобы ошибка не меняла рабочую сессию.
  std::shared_ptr<Board> planningBoard = Board::create(board->countColumns(), board->countRows());
  for (const std::shared_ptr<Figure> & figure : board->getFigures())
    planningBoard->add(cloneFigureForPlanning(figure));

  FigurePool planningPool = figurePool;
  std::vector<std::shared_ptr<Figure>> placements;
  while (planningPool.hasPreview())
  {
    const AutomaticPlacementProposal proposal = strategy.proposePlacement(planningBoard, planningPool.previewFigure());
    if (!proposal.validation.canPlace())
      return {{}, proposal.validation};
    if (!proposal.figure)
      return {{}, {PlacementValidationCode::InvalidFigure}};

    // Каждый успешный вариант сразу попадает на временную доску, чтобы следующая фигура видела занятое место.
    placements.push_back(proposal.figure);
    planningBoard->add(proposal.figure);
    planningPool.takePreview();
  }

  return {std::move(placements), {PlacementValidationCode::Ok}};
}
