#include <algs.h>
#include <board.h>
#include <boardanalyzer.h>
#include <gtest/gtest.h>
#include <rect2d.h>

// FigureIntersect

// Фигуры не пересекаются
TEST(FigureIntersect, FigureNotIntersect)
{
  Figure firstFigure = OShape(Point2D{0, 0}, 1, 1);
  Figure secondFigure = OShape(Point2D{0, 0}, 6, 6);

  EXPECT_FALSE(FigureIntersect(firstFigure, secondFigure));
}

// Фигуры касаются друг друга по границе
TEST(FigureIntersect, BoxIntersect_FigureNotIntersect)
{
  Figure firstFigure = IShape(Point2D{0, 0}, 3, 2);
  Figure secondFigure = LShape(Point2D{0, 0}, 2, 4);

  EXPECT_FALSE(FigureIntersect(firstFigure, secondFigure));
}

// Фигуры пересекаются
TEST(FigureIntersect, BoxIntersect_FigureIntersect)
{
  Figure firstFigure = IShape(Point2D{0, 0}, 3, 3);
  Figure secondFigure = LShape(Point2D{0, 0}, 2, 3);

  EXPECT_TRUE(FigureIntersect(firstFigure, secondFigure));
}

// Фигуры совпадают
TEST(FigureIntersect, FigureInFigure)
{
  Figure firstFigure = OShape(Point2D{0, 0}, 3, 3);
  Figure secondFigure = OShape(Point2D{0, 0}, 3, 3);

  EXPECT_TRUE(FigureIntersect(firstFigure, secondFigure));
}

// Фигуры касаются друг друга углами
TEST(FigureIntersect, FigureNotIntersectByAngles)
{
  Figure firstFigure = OShape(Point2D{0, 0}, 1, 1);
  Figure secondFigure = OShape(Point2D{0, 0}, 3, 3);

  EXPECT_FALSE(FigureIntersect(firstFigure, secondFigure));
}

// Фигуры пересекаются после поворота
TEST(FigureIntersect, FigureIntersectAfterRotate)
{
  Figure firstFigure = LShape(Point2D{0, 0}, 3, 3);
  Figure secondFigure = JShape(Point2D{0, 0}, 4, 4);

  secondFigure.Rotate();

  EXPECT_TRUE(FigureIntersect(firstFigure, secondFigure));
}


// BoundingBoxInetrsect

// Фигура выходит за левую границу доски
TEST(BoundingBoxContains, LeftIntersect)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, -1, 4);

  EXPECT_FALSE(BoundingBoxContains(*board, figure));
}

// Фигура выходит за правую границу доски
TEST(BoundingBoxContains, RightIntersect)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 9, 4);

  EXPECT_FALSE(BoundingBoxContains(*board, figure));
}

// Фигура выходит за верхнюю границу доски
TEST(BoundingBoxContains, TopInetersect)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 4, -1);

  EXPECT_FALSE(BoundingBoxContains(*board, figure));
}

// Фигура выходит за нижнюю границу доски
TEST(BoundingBoxContains, BottomInetersect)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 2, -1);

  EXPECT_FALSE(BoundingBoxContains(*board, figure));
}

// Фигура касается границы доски
TEST(BoundingBoxContains, TouchBoundary)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 8, 4);

  EXPECT_TRUE(BoundingBoxContains(*board, figure));
}

// Фигура касается границы доски с двух сторон
TEST(BoundingBoxContains, TouchBothBoundaries)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 8, 0);

  EXPECT_TRUE(BoundingBoxContains(*board, figure));
}

// Фигура находится за пределами доски
TEST(BoundingBoxContains, NotInBorder)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, -3, 4);

  EXPECT_FALSE(BoundingBoxContains(*board, figure));
}

// Фигура полностью внутри доски
TEST(BoundingBoxContains, CompletelyInBorder)
{
  std::shared_ptr<Board> board = Board::create(10, 10);
  Figure figure = OShape(Point2D{0, 0}, 3, 3);

  EXPECT_TRUE(BoundingBoxContains(*board, figure));
}

// Анализатор учитывает только клетки внутри доски
TEST(BoardAnalyzer, CountsOnlyCellsInsideBoard)
{
  std::shared_ptr<Board> board = Board::create(2, 2);
  std::shared_ptr<BoardAnalyzer> analyzer = BoardAnalyzer::create(board);
  std::shared_ptr<Figure> figure = std::make_shared<OShape>(Point2D{0, 0}, -1, 0);

  board->add(figure);

  EXPECT_EQ(analyzer->countOccupiedCells(), 2);
  EXPECT_EQ(analyzer->countEmptyCells(), 2);
}
