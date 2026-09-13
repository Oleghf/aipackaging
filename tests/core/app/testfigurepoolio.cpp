#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include <gtest/gtest.h>

#include "../../../src/core/app/rotatecommand.h"
#include "../../../src/core/app/services/io/objectpoolloader.h"
#include "../../../src/core/app/services/io/objectpoolsaver.h"
#include "../../../src/core/app/services/packing/figurepool.h"
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

std::multiset<std::pair<int, int>> toCoordinateSet(const std::shared_ptr<Figure> & figure)
{
  std::multiset<std::pair<int, int>> result;
  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    result.emplace(coords.column, coords.row);
  }

  return result;
}

size_t uniqueCoordinateCount(const Figure & figure)
{
  std::set<std::pair<int, int>> uniqueCoordinates;
  for (const Cell2D & cell : figure.GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    uniqueCoordinates.emplace(coords.column, coords.row);
  }

  return uniqueCoordinates.size();
}
} // namespace

//------------------------------------------------------------------------------
/**
  Проверяет появление предварительного просмотра после загрузки в пул
*/
//--
TEST(FigurePool, LoadPromotesPreview)
{
  FigurePool pool;
  std::shared_ptr<Figure> first = makeFigure({{0, 0}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}});

  pool.load({first, second});

  EXPECT_TRUE(pool.hasPreview());
  EXPECT_EQ(pool.snapshotRemaining().size(), 2);
}


//------------------------------------------------------------------------------
/**
  Проверяет уменьшение пула после выдачи предварительного просмотра
*/
//--
TEST(FigurePool, TakePreviewShrinksRemainingPool)
{
  FigurePool pool;
  std::shared_ptr<Figure> first = makeFigure({{0, 0}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}});

  pool.load({first, second});
  std::shared_ptr<Figure> taken = pool.takePreview();

  EXPECT_NE(taken, nullptr);
  EXPECT_EQ(pool.snapshotRemaining().size(), 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет безопасную работу пустого пула
*/
//--
//------------------------------------------------------------------------------
/**
  Проверяет возврат фигуры как текущей фигуры предварительного просмотра
*/
//--
TEST(FigurePool, ReturnAsPreviewMakesFigureCurrentPreviewAndPreservesOldPreview)
{
  FigurePool pool(42);
  std::shared_ptr<Figure> oldPreview = makeFigure({{0, 0}});
  std::shared_ptr<Figure> returned = makeFigure({{1, 0}});
  pool.load({oldPreview});

  pool.returnAsPreview(returned);

  ASSERT_TRUE(pool.hasPreview());
  EXPECT_EQ(pool.previewFigure(), returned);
  const std::vector<std::shared_ptr<Figure>> snapshot = pool.snapshotRemaining();
  ASSERT_EQ(snapshot.size(), 2);
  EXPECT_EQ(snapshot.front(), returned);
  EXPECT_NE(std::find(snapshot.begin(), snapshot.end(), oldPreview), snapshot.end());
}


TEST(FigurePool, EmptyPoolIsSafe)
{
  FigurePool pool;

  EXPECT_TRUE(pool.isEmpty());
  EXPECT_FALSE(pool.hasPreview());
  EXPECT_EQ(pool.takePreview(), nullptr);
  EXPECT_TRUE(pool.snapshotRemaining().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет одинаковые предварительные фигуры при общем начальном значении
*/
//--
TEST(FigurePool, SeededPoolsUseSamePreviewSequence)
{
  std::shared_ptr<Figure> first = makeFigure({{0, 0}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}});
  std::shared_ptr<Figure> third = makeFigure({{2, 0}});
  const std::vector<std::shared_ptr<Figure>> figures = {first, second, third};
  FigurePool firstPool(42);
  FigurePool secondPool(42);

  firstPool.load(figures);
  secondPool.load(figures);

  EXPECT_EQ(firstPool.previewFigure(), secondPool.previewFigure());
  EXPECT_EQ(firstPool.takePreview(), secondPool.takePreview());
  EXPECT_EQ(firstPool.previewFigure(), secondPool.previewFigure());
  EXPECT_EQ(firstPool.takePreview(), secondPool.takePreview());
  EXPECT_EQ(firstPool.previewFigure(), secondPool.previewFigure());
  EXPECT_EQ(firstPool.snapshotRemaining().size(), secondPool.snapshotRemaining().size());
}


//------------------------------------------------------------------------------
/**
  Проверяет парсинг одного OBJECT блока
*/
//--
TEST(ObjectPoolLoader, ParsesSingleObjectBlock)
{
  const auto result = ObjectPoolLoader::loadFromText("OBJECT\nCELL 0 0\nCELL 1 0\nEND_OBJECT\n");

  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.figures.size(), 1);
  EXPECT_EQ(toCoordinateSet(result.figures.front()), (std::multiset<std::pair<int, int>>{{0, 0}, {1, 0}}));
}


//------------------------------------------------------------------------------
/**
  Проверяет парсинг нескольких OBJECT блоков
*/
//--
TEST(ObjectPoolLoader, ParsesMultipleObjectBlocks)
{
  const auto result = ObjectPoolLoader::loadFromText("OBJECT\nCELL 0 0\nEND_OBJECT\n"
                                                     "OBJECT\nCELL 1 0\nCELL 1 1\nEND_OBJECT\n");

  ASSERT_TRUE(result.success);
  EXPECT_EQ(result.figures.size(), 2);
}


//------------------------------------------------------------------------------
/**
  Проверяет ошибку на некорректном формате
*/
//--
TEST(ObjectPoolLoader, FailsOnInvalidFormat)
{
  const auto result = ObjectPoolLoader::loadFromText("CELL 0 0\n");

  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.errorMessage.empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет сериализацию оставшегося пула в формат объектов
*/
//--
TEST(ObjectPoolSaver, SavesRemainingPoolToText)
{
  const std::string text = ObjectPoolSaver::saveToText({makeFigure({{0, 0}, {1, 0}}), makeFigure({{2, 1}})});

  EXPECT_NE(text.find("OBJECT"), std::string::npos);
  EXPECT_NE(text.find("CELL 0 0"), std::string::npos);
  EXPECT_NE(text.find("CELL 2 1"), std::string::npos);
}


//------------------------------------------------------------------------------
/**
  Проверяет двустороннее преобразование между модулем сохранения и загрузчиком
*/
//--
TEST(ObjectPoolSaver, SupportsRoundTripWithLoader)
{
  const std::vector<std::shared_ptr<Figure>> source = {makeFigure({{0, 0}, {1, 0}, {1, 1}}), makeFigure({{2, 2}})};

  const std::string text = ObjectPoolSaver::saveToText(source);
  const auto loaded = ObjectPoolLoader::loadFromText(text);

  ASSERT_TRUE(loaded.success);
  ASSERT_EQ(loaded.figures.size(), source.size());
  EXPECT_EQ(toCoordinateSet(loaded.figures[0]), toCoordinateSet(source[0]));
  EXPECT_EQ(toCoordinateSet(loaded.figures[1]), toCoordinateSet(source[1]));
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение пула фигур в файл
*/
//--
TEST(ObjectPoolSaver, SavesToFile)
{
  const std::filesystem::path filePath = std::filesystem::temp_directory_path() / "aipackaging_pool_test.txt";
  const bool isSaved = ObjectPoolSaver::saveToFile(filePath.string(), {makeFigure({{0, 0}})});

  ASSERT_TRUE(isSaved);
  EXPECT_TRUE(std::filesystem::exists(filePath));

  std::filesystem::remove(filePath);
}


//------------------------------------------------------------------------------
/**
  Проверяет создание общей фигуры из набора `Cell2D`
*/
//--
TEST(ObjectFigure, SupportsMoveRotateAndPoints)
{
  std::vector<Cell2D> cells;
  cells.push_back(CreateSquareCell({0, 0}));
  cells.push_back(CreateSquareCell({1, 0}));
  ObjectFigure figure(Point2D{5, -5}, std::move(cells));

  figure.Move(FigureMove::RIGHT);
  figure.Rotate();

  EXPECT_EQ(uniqueCoordinateCount(figure), figure.GetCells().size());
  EXPECT_FALSE(figure.GetPoints(100).empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение уникальных клеток после поворота Z-фигуры
*/
//--
TEST(ZShape, RotatePreservesUniqueCells)
{
  ZShape figure(Point2D{5, -5}, 0, 0);

  figure.Rotate();

  EXPECT_EQ(uniqueCoordinateCount(figure), figure.GetCells().size());
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение уникальных клеток после поворота S-фигуры
*/
//--
TEST(SShape, RotatePreservesUniqueCells)
{
  SShape figure(Point2D{5, -5}, 0, 0);

  figure.Rotate();

  EXPECT_EQ(uniqueCoordinateCount(figure), figure.GetCells().size());
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение уникальных клеток после поворота J-фигуры
*/
//--
TEST(JShape, RotatePreservesUniqueCells)
{
  JShape figure(Point2D{5, -5}, 0, 0);

  figure.Rotate();

  EXPECT_EQ(uniqueCoordinateCount(figure), figure.GetCells().size());
}


//------------------------------------------------------------------------------
/**
  Проверяет возврат фигуры к исходным координатам после четырёх поворотов
*/
//--
TEST(Figure, FourRotationsRestoreOriginalCoordinates)
{
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}, {1, 1}, {2, 1}});
  const std::multiset<std::pair<int, int>> before = toCoordinateSet(figure);

  for (size_t i = 0; i < 4; i++)
    figure->Rotate();

  EXPECT_EQ(toCoordinateSet(figure), before);
}


//------------------------------------------------------------------------------
/**
  Проверяет восстановление исходных координат откатом RotateCommand
*/
//--
TEST(RotateCommand, UndoRestoresOriginalCoordinates)
{
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}, {1, 1}, {2, 1}});
  const std::multiset<std::pair<int, int>> before = toCoordinateSet(figure);

  RotateCommand command(figure);
  command.execute();
  command.undo();

  EXPECT_EQ(toCoordinateSet(figure), before);
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие ложного попадания при проверке разнесённых клеток
*/
//--
TEST(Figure, ContainsDoesNotHitPhantomBridge)
{
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {2, 0}});

  EXPECT_FALSE(figure->Contains({155, -5}, 1.0));
}
