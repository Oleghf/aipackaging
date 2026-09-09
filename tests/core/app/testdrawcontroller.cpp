#include <algorithm>

#include <board.h>
#include <color.h>
#include <drawcontroller.h>
#include <gtest/gtest.h>
#include <iview.h>
#include <primitiveview.h>
#include <rect2d.h>
#include <scenepaintevent.h>
#include <selectionmodel.h>
#include <vector2d.h>

#include "../../../src/core/domain/algorithms/objectfigure.h"

namespace
{
std::shared_ptr<Figure> makeFigure(std::initializer_list<Coordinates> coords)
{
  std::vector<Cell2D> cells;
  cells.reserve(coords.size());

  for (const Coordinates & coord : coords)
    cells.push_back(CreateSquareCell(coord));

  return std::make_shared<ObjectFigure>(Point2D{5, -5}, std::move(cells));
}

std::shared_ptr<Figure> makeFigureAt(const Point2D & topLeft, std::initializer_list<Coordinates> coords)
{
  std::vector<Cell2D> cells;
  cells.reserve(coords.size());

  for (const Coordinates & coord : coords)
    cells.push_back(CreateSquareCell(coord));

  return std::make_shared<ObjectFigure>(topLeft, std::move(cells));
}

struct LineSegment
{
  Point2D from;
  Point2D to;
  Color color;
};

bool sameColor(const Color & left, const Color & right)
{
  return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

////////////////////////////////////////////////////////////////////////////////
//
/// Заглушка перерисовки
/**
	*/
////////////////////////////////////////////////////////////////////////////////
class RedrawViewStub : public IRedrawView
{
public:
  void requestRedraw() override { ++redrawRequests; }

  size_t redrawRequests = 0;
};

////////////////////////////////////////////////////////////////////////////////
//
/// Шпион примитивного отрисовщика
/**
	*/
////////////////////////////////////////////////////////////////////////////////
class PrimitiveViewSpy : public PrimitiveView
{
public:
  void line(const Point2D & p1, const Point2D & p2) override { lines.push_back({p1, p2, currentPenColor}); }

  void circle(const Point2D & center, double radius) override {}

  void rect(const Rect2D & rect) override {}

  void setPenColor(const Color & color) override { currentPenColor = color; }

  void setBrushColor(const Color & color) override {}

  void setThickness(double thickness) override {}

  void scale(double factorx, double factory) override {}

  void translate(const Vector2D & offset) override {}

  bool hasLine(const Point2D & from, const Point2D & to) const
  {
    for (const LineSegment & line : lines)
    {
      if (line.from == from && line.to == to)
        return true;
    }

    return false;
  }

  size_t lineCountWithColor(const Color & color) const
  {
    return std::count_if(lines.begin(), lines.end(), [&color](const LineSegment & line) { return sameColor(line.color, color); });
  }

  Color currentPenColor = Color::BLACK();
  std::vector<LineSegment> lines;
};
} // namespace


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие мостов между разнесёнными ячейками при отрисовке
*/
//--
TEST(DrawController, DoesNotBridgeSeparateCells)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {2, 0}});
  ASSERT_TRUE(genBoard->add(figure));

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = genBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Generate, region);

  controller.onPaintEvent(event);

  EXPECT_EQ(painter.lines.size(), 8);
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие фантомного сегмента между разными ячейками object-фигуры
*/
//--
TEST(DrawController, ObjectFigureDoesNotProducePhantomSegments)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {2, 0}});
  ASSERT_TRUE(genBoard->add(figure));

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = genBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Generate, region);

  controller.onPaintEvent(event);

  EXPECT_FALSE(painter.hasLine({5, -5}, {205, -5}));
}


//------------------------------------------------------------------------------
/**
  Проверяет, что отрисовка не запрашивает вложенную перерисовку
*/
//--
TEST(DrawController, MainPaintDoesNotRequestNestedRedraw)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}});
  ASSERT_TRUE(mainBoard->add(figure));

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = mainBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Main, region);

  controller.onPaintEvent(event);

  EXPECT_EQ(view->redrawRequests, 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет зелёную отрисовку валидной выбранной фигуры
*/
//--
TEST(DrawController, SelectedValidFigureIsGreen)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  ASSERT_TRUE(mainBoard->add(figure));
  selection->add(figure);

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = mainBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Main, region);

  controller.onPaintEvent(event);

  EXPECT_GT(painter.lineCountWithColor(Color::GREEN()), 0);
  EXPECT_EQ(painter.lineCountWithColor(Color::RED()), 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет красную отрисовку невалидной выбранной фигуры
*/
//--
TEST(DrawController, SelectedInvalidFigureIsRed)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{30, 30}});
  ASSERT_TRUE(mainBoard->add(figure));
  selection->add(figure);

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = mainBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Main, region);

  controller.onPaintEvent(event);

  EXPECT_GT(painter.lineCountWithColor(Color::RED()), 0);
  EXPECT_EQ(painter.lineCountWithColor(Color::GREEN()), 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет обычную отрисовку невыбранной фигуры
*/
//--
TEST(DrawController, NonSelectedFigureUsesDefaultColor)
{
  std::shared_ptr<RedrawViewStub> view = std::make_shared<RedrawViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(20, 20);
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  ASSERT_TRUE(mainBoard->add(figure));

  PrimitiveViewSpy painter;
  DrawController controller(view, selection, mainBoard, genBoard);
  const Rect2D region = mainBoard->boundingBox(100);
  ScenePaintEvent event(painter, ScenePaint::Main, region);

  controller.onPaintEvent(event);

  EXPECT_GT(painter.lineCountWithColor(Color::BLACK()), 0);
  EXPECT_EQ(painter.lineCountWithColor(Color::GREEN()), 0);
  EXPECT_EQ(painter.lineCountWithColor(Color::RED()), 0);
}
