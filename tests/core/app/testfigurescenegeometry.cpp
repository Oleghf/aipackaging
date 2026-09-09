#include <gtest/gtest.h>

#include "../../../src/core/app/services/figurescenegeometry.h"
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
} // namespace

//------------------------------------------------------------------------------
/**
  Проверяет преобразование точек клетки в координаты сцены
*/
//--
TEST(FigureSceneGeometry, BuildsScenePointGroupsFromFigureCells)
{
  std::shared_ptr<Figure> figure = makeFigure({{2, 3}});

  const std::vector<std::vector<Point2D>> groups = FigureSceneGeometry::cellPointGroups(*figure, 100);

  ASSERT_EQ(groups.size(), 1);
  ASSERT_EQ(groups.front().size(), 4);
  EXPECT_EQ(groups.front()[0], (Point2D{205, 395}));
  EXPECT_EQ(groups.front()[1], (Point2D{205, 295}));
  EXPECT_EQ(groups.front()[2], (Point2D{305, 295}));
  EXPECT_EQ(groups.front()[3], (Point2D{305, 395}));
}
