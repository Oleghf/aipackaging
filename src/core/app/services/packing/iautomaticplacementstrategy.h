#ifndef IAUTOMATICPLACEMENTSTRATEGY_H__
#define IAUTOMATICPLACEMENTSTRATEGY_H__

#include <memory>

#include <icommand.h>
#include <packing/placementvalidator.h>

class Board;
class Figure;
class FigurePool;
class PreviewPoolPresenter;
class SelectionModel;

struct AutomaticPlacementResult
{
  std::unique_ptr<ICommand> command;
  PlacementValidationResult validation;

  bool hasFailure() const { return !command && !validation.canPlace(); }
};

struct AutomaticPlacementProposal
{
  std::shared_ptr<Figure> figure;
  PlacementValidationResult validation;

  bool hasFailure() const { return !figure && !validation.canPlace(); }
};

////////////////////////////////////////////////////////////////////////////////
//
/// Интерфейс стратегий автоматического размещения фигур предпросмотра.
/**
*/
////////////////////////////////////////////////////////////////////////////////
class IAutomaticPlacementStrategy
{
public:
  virtual ~IAutomaticPlacementStrategy() = default;

  virtual AutomaticPlacementProposal proposePlacement(const std::shared_ptr<Board> & board,
                                                      const std::shared_ptr<Figure> & previewFigure) const = 0;

  virtual AutomaticPlacementResult createPlacement(const std::shared_ptr<Board> & board, FigurePool & figurePool,
                                                   const PreviewPoolPresenter & previewPresenter,
                                                   const std::shared_ptr<SelectionModel> & selection,
                                                   std::shared_ptr<Figure> * activeFigure) const = 0;
};

#endif
