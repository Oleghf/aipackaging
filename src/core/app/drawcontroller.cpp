#include <algorithm>
#include <cmath>

#include <algs.h>
#include <board.h>
#include <color.h>
#include <drawcontroller.h>
#include <figurescenegeometry.h>
#include <iview.h>
#include <placementvalidator.h>
#include <primitiveview.h>
#include <scenepaintevent.h>
#include <selectionmodel.h>
#include <vector2d.h>


namespace
{
constexpr double CELL_SIZE = 100;
}


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
DrawController::DrawController(std::shared_ptr<IRedrawView> view, std::shared_ptr<SelectionModel> selection,
                               std::shared_ptr<Board> main, std::shared_ptr<Board> gen)
  : view_(std::move(view))
  , selection_(std::move(selection))
  , mainBoard_(std::move(main))
  , genBoard_(std::move(gen))
{
}


//------------------------------------------------------------------------------
/**
  Обрабатывает событие перерисовки сцен
*/
//--
void DrawController::onPaintEvent(const ScenePaintEvent & event)
{
  PrimitiveView & painter = event.painter();

  PlacementValidator placementValidator;

  auto selectedValidPred = [this, &placementValidator](std::shared_ptr<Figure> fig)
  {
    if (!selection_->findFigure(fig))
      return false;
    return placementValidator.validate(mainBoard_, fig).canPlace();
  };

  auto selectedInvalidPred = [this, &placementValidator](std::shared_ptr<Figure> fig)
  {
    if (!selection_->findFigure(fig))
      return false;
    return !placementValidator.validate(mainBoard_, fig).canPlace();
  };

  auto notSelectedPred = [this](std::shared_ptr<Figure> fig)
  {
    if (selection_->findFigure(fig))
      return false;
    return true;
  };

  if (event.whatScene() == ScenePaint::Main)
  {
    sceneToScreenTransform(painter, mainBoard_->boundingBox(CELL_SIZE), event.region());

    drawGrid(painter, mainBoard_->getPrimitivesGrid(CELL_SIZE));
    painter.setThickness(10);

    std::vector<std::shared_ptr<Figure>> selectedValidFigures = mainBoard_->getFigures(selectedValidPred);
    std::vector<std::shared_ptr<Figure>> selectedInvalidFigures = mainBoard_->getFigures(selectedInvalidPred);
    std::vector<std::shared_ptr<Figure>> notSelectedFigures = mainBoard_->getFigures(notSelectedPred);

    painter.setPenColor(Color::BLACK());
    std::for_each(notSelectedFigures.begin(), notSelectedFigures.end(),
                  [this, &painter](std::shared_ptr<Figure> fig) { drawFigure(painter, fig); });
    painter.setPenColor(Color::GREEN());
    std::for_each(selectedValidFigures.begin(), selectedValidFigures.end(),
                  [this, &painter](std::shared_ptr<Figure> fig) { drawFigure(painter, fig); });
    painter.setPenColor(Color::RED());
    std::for_each(selectedInvalidFigures.begin(), selectedInvalidFigures.end(),
                  [this, &painter](std::shared_ptr<Figure> fig) { drawFigure(painter, fig); });
  }
  else
  {
    sceneToScreenTransform(painter, genBoard_->boundingBox(CELL_SIZE), event.region());

    painter.setThickness(10);
    painter.setPenColor(Color::BLACK());
    std::vector<std::shared_ptr<Figure>> notSelectedFigures = genBoard_->getFigures(notSelectedPred);
    std::for_each(notSelectedFigures.begin(), notSelectedFigures.end(),
                  [this, &painter](std::shared_ptr<Figure> fig) { drawFigure(painter, fig); });
  }
}


//------------------------------------------------------------------------------
/**
  Настраивает отрисовщик таким образом, чтобы он рисовал в экранных координатах сцены
*/
//--
void DrawController::sceneToScreenTransform(PrimitiveView & view, const Rect2D & scene, const Rect2D & screen)
{
  double screenWidth = screen.right() - screen.left();
  double screenHeight = screen.bottom() - screen.top();
  double realWidth = scene.right() - scene.left();
  double realHeight = scene.bottom() - scene.top();

  view.translate({screen.left(), screen.top()});
  view.scale(screenWidth / realWidth, screenHeight / realHeight);
  view.translate({-scene.left(), -scene.top()});
}


//
void DrawController::drawFigure(PrimitiveView & painter, std::shared_ptr<Figure> figure)
{
  for (const std::vector<Point2D> & cellPoints : FigureSceneGeometry::cellPointGroups(*figure, CELL_SIZE))
  {
    for (size_t i = 0; i < cellPoints.size(); ++i)
      painter.line(cellPoints[i], cellPoints[(i + 1) % cellPoints.size()]);
  }
}


//
void DrawController::drawGrid(PrimitiveView & painter, std::vector<Line2D> gridPrimitives)
{
  for (const Line2D & line : gridPrimitives)
    painter.line(line.getPoint(0), line.getPoint(9999));
}
