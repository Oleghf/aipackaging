#include <utility>
#include <vector>

#include <cell2d.h>
#include <objectfigure.h>
#include <packing/figurepool.h>
#include <packing/previewautomaticplacementstrategy.h>
#include <packing/previewpoolpresenter.h>
#include <placepreviewfigurecommand.h>
#include <point2d.h>

namespace
{
constexpr Point2D MAIN_BOARD_TOP_LEFT{0, 0};

std::shared_ptr<Figure> makeMainBoardCandidate(const std::shared_ptr<Figure> & previewFigure)
{
  if (!previewFigure)
    return nullptr;

  std::vector<Cell2D> cells;
  cells.reserve(previewFigure->GetCells().size());

  for (const Cell2D & cell : previewFigure->GetCells())
    cells.push_back(CreateSquareCell(cell.GetCoordinates()));

  return std::make_shared<ObjectFigure>(MAIN_BOARD_TOP_LEFT, std::move(cells));
}
} // namespace


AutomaticPlacementProposal
PreviewAutomaticPlacementStrategy::proposePlacement(const std::shared_ptr<Board> & board,
                                                    const std::shared_ptr<Figure> & previewFigure) const
{
  const std::shared_ptr<Figure> placementFigure = makeMainBoardCandidate(previewFigure);
  const PlacementValidationResult validation = validator_.validate(board, placementFigure);
  if (!validation.canPlace())
    return {nullptr, validation};

  return {placementFigure, validation};
}


AutomaticPlacementResult PreviewAutomaticPlacementStrategy::createPlacement(const std::shared_ptr<Board> & board,
                                                                            FigurePool & figurePool,
                                                                            const PreviewPoolPresenter & previewPresenter,
                                                                            const std::shared_ptr<SelectionModel> & selection,
                                                                            std::shared_ptr<Figure> * activeFigure) const
{
  AutomaticPlacementProposal proposal = proposePlacement(board, figurePool.previewFigure());
  if (!proposal.validation.canPlace())
    return {nullptr, proposal.validation};

  return {std::make_unique<PlacePreviewFigureCommand>(board, figurePool, previewPresenter, selection, activeFigure,
                                                      proposal.figure, PreviewConsumptionPolicy::ConsumePreview),
          proposal.validation};
}
