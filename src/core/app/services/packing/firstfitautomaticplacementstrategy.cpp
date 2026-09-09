#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include <board.h>
#include <cell2d.h>
#include <objectfigure.h>
#include <packing/figurepool.h>
#include <packing/firstfitautomaticplacementstrategy.h>
#include <packing/previewpoolpresenter.h>
#include <placepreviewfigurecommand.h>
#include <point2d.h>

namespace
{
constexpr Point2D MAIN_BOARD_TOP_LEFT{0, 0};

std::vector<Coordinates> normalizedCoordinates(const std::shared_ptr<Figure> & figure)
{
  std::vector<Coordinates> coordinates;
  if (!figure || figure->GetCells().empty())
    return coordinates;

  int minColumn = std::numeric_limits<int>::max();
  int minRow = std::numeric_limits<int>::max();
  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    minColumn = std::min(minColumn, coords.column);
    minRow = std::min(minRow, coords.row);
  }

  coordinates.reserve(figure->GetCells().size());
  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    coordinates.push_back({coords.column - minColumn, coords.row - minRow});
  }

  return coordinates;
}

std::shared_ptr<Figure> makeCandidate(const std::vector<Coordinates> & localCoordinates, int column, int row)
{
  std::vector<Cell2D> cells;
  cells.reserve(localCoordinates.size());
  for (const Coordinates & coords : localCoordinates)
    cells.push_back(CreateSquareCell({coords.column + column, coords.row + row}));

  return std::make_shared<ObjectFigure>(MAIN_BOARD_TOP_LEFT, std::move(cells));
}
} // namespace


AutomaticPlacementProposal
FirstFitAutomaticPlacementStrategy::proposePlacement(const std::shared_ptr<Board> & board,
                                                     const std::shared_ptr<Figure> & previewFigure) const
{
  if (!board)
    return {nullptr, {PlacementValidationCode::InvalidBoard}};
  if (!previewFigure)
    return {nullptr, {PlacementValidationCode::InvalidFigure}};

  // Фигура предпросмотра может иметь смещение сцены генерации; стратегия работает только с локальной формой.
  const std::vector<Coordinates> localCoordinates = normalizedCoordinates(previewFigure);
  if (localCoordinates.empty())
    return {nullptr, {PlacementValidationCode::EmptyFigure}};

  // Детерминированный первый подходящий вариант: строки сверху вниз, колонки слева направо.
  for (size_t row = 0; row < board->countRows(); ++row)
  {
    for (size_t column = 0; column < board->countColumns(); ++column)
    {
      std::shared_ptr<Figure> candidate = makeCandidate(localCoordinates, static_cast<int>(column), static_cast<int>(row));
      const PlacementValidationResult validation = validator_.validate(board, candidate);
      if (validation.canPlace())
        return {candidate, validation};
    }
  }

  return {nullptr, {PlacementValidationCode::NoPlacementFound}};
}


AutomaticPlacementResult FirstFitAutomaticPlacementStrategy::createPlacement(const std::shared_ptr<Board> & board,
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
