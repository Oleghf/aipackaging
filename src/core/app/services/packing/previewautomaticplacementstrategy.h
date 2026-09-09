#ifndef PREVIEWAUTOMATICPLACEMENTSTRATEGY_H__
#define PREVIEWAUTOMATICPLACEMENTSTRATEGY_H__

#include <packing/iautomaticplacementstrategy.h>
#include <packing/placementvalidator.h>

////////////////////////////////////////////////////////////////////////////////
//
/// Минимальная стратегия автоматического размещения в текущую позицию предпросмотра.
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PreviewAutomaticPlacementStrategy : public IAutomaticPlacementStrategy
{
public:
  AutomaticPlacementProposal proposePlacement(const std::shared_ptr<Board> & board,
                                              const std::shared_ptr<Figure> & previewFigure) const override;

  AutomaticPlacementResult createPlacement(const std::shared_ptr<Board> & board, FigurePool & figurePool,
                                           const PreviewPoolPresenter & previewPresenter,
                                           const std::shared_ptr<SelectionModel> & selection,
                                           std::shared_ptr<Figure> * activeFigure) const override;

private:
  PlacementValidator validator_;
};

#endif
