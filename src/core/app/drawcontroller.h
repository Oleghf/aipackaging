#ifndef DRAWCONTROLLER_H__
#define DRAWCONTROLLER_H__

#include <memory>
#include <vector>

#include <iscenepainthandler.h>

class Rect2D;
class Line2D;
class IRedrawView;
class SelectionModel;
class GeometryModel;
class ScenePaintEvent;
class PrimitiveView;
class Figure;
class Board;

////////////////////////////////////////////////////////////////////////////////
//
/// Контроллер, отвечающий за отрисовку
/**
*/
////////////////////////////////////////////////////////////////////////////////
class DrawController : public IScenePaintHandler
{
public:
  DrawController(std::shared_ptr<IRedrawView> view, std::shared_ptr<SelectionModel> selection, std::shared_ptr<Board> main,
                 std::shared_ptr<Board> gen);

  // Обработать ивент отрисовки
  void onPaintEvent(const ScenePaintEvent & event) override;

private:
  void sceneToScreenTransform(PrimitiveView & view, const Rect2D & scene, const Rect2D & screen);
  void drawFigure(PrimitiveView & painter, std::shared_ptr<Figure> figure);
  void drawGrid(PrimitiveView & painter, std::vector<Line2D> gridPrimitives);

private:
  std::shared_ptr<IRedrawView> view_;

  std::shared_ptr<SelectionModel> selection_;

  std::shared_ptr<Board> mainBoard_;
  std::shared_ptr<Board> genBoard_;
};

#endif
