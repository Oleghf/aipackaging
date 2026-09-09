#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

#include <actionevent.h>
#include <board.h>
#include <gtest/gtest.h>
#include <iview.h>
#include <loadfileevent.h>
#include <openscenefileevent.h>
#include <redoevent.h>
#include <savefileevent.h>
#include <scenemouseevent.h>
#include <selectionmodel.h>
#include <undoevent.h>

#include "../../../src/core/app/autopackcommand.h"
#include "../../../src/core/app/commandmanager.h"
#include "../../../src/core/app/deletefigurecommand.h"
#include "../../../src/core/app/maincontroller.h"
#include "../../../src/core/app/movecommand.h"
#include "../../../src/core/app/packagingstate.h"
#include "../../../src/core/app/packingcontroller.h"
#include "../../../src/core/app/placepreviewfigurecommand.h"
#include "../../../src/core/app/rotatecommand.h"
#include "../../../src/core/app/services/io/objectpoolloader.h"
#include "../../../src/core/app/services/io/packingscenesaver.h"
#include "../../../src/core/app/services/packing/automaticpackplanner.h"
#include "../../../src/core/app/services/packing/figurepool.h"
#include "../../../src/core/app/services/packing/firstfitautomaticplacementstrategy.h"
#include "../../../src/core/app/services/packing/packingactionmapper.h"
#include "../../../src/core/app/services/packing/placementvalidator.h"
#include "../../../src/core/app/services/packing/previewautomaticplacementstrategy.h"
#include "../../../src/core/app/services/packing/previewpoolpresenter.h"
#include "../../../src/core/domain/algorithms/objectfigure.h"

namespace
{
////////////////////////////////////////////////////////////////////////////////
//
/// Заглушка представления статистики
/**
	*/
////////////////////////////////////////////////////////////////////////////////
class StatisticsViewStub : public IStatisticsView
{
public:
  // Обновить общее количество клеток
  void statisticChangeCountAllCells(unsigned int allCells) override { allCells_ = allCells; }

  // Обновить количество занятых клеток
  void statisticChangeCountOccupiedCells(unsigned int occupiedCells) override { occupiedCells_ = occupiedCells; }

  unsigned int allCells_ = 0;
  unsigned int occupiedCells_ = 0;
};

////////////////////////////////////////////////////////////////////////////////
//
/// Заглушка полного представления приложения
/**
	*/
////////////////////////////////////////////////////////////////////////////////
class MainViewStub : public IView
{
public:
  void statisticChangeCountAllCells(unsigned int allCells) override { allCells_ = allCells; }

  void statisticChangeCountOccupiedCells(unsigned int occupiedCells) override { occupiedCells_ = occupiedCells; }

  std::string openSaveFileDialog(const std::string & title, const std::string & initPath,
                                 const std::string & filter = {}) override
  {
    return savePath_;
  }

  std::string openLoadFileDialog(const std::string & title, const std::string & initPath,
                                 const std::string & filter = {}) override
  {
    return loadPath_;
  }

  void requestRedraw() override { ++redrawRequests_; }

  void setActionEnabled(PackagingAction action, bool isEnabled) override {}

  bool isActionEnabled(PackagingAction action) const override { return true; }

  void showMessage(const std::string & title, const std::string & message, MessageType type) override
  {
    lastMessageTitle_ = title;
    lastMessageText_ = message;
    lastMessageType_ = type;
    ++messageCount_;
  }

  void addEventListener(std::shared_ptr<EventListener> listener) override {}

  void removeEventListener(std::shared_ptr<EventListener> listener) override {}

  void setZoomFactor(double factor) override { zoomFactor_ = factor; }

  double zoomFactor() const override { return zoomFactor_; }

  std::string savePath_;
  std::string loadPath_;
  std::string lastMessageTitle_;
  std::string lastMessageText_;
  MessageType lastMessageType_ = MessageType::Info;
  size_t messageCount_ = 0;
  unsigned int allCells_ = 0;
  unsigned int occupiedCells_ = 0;
  size_t redrawRequests_ = 0;
  double zoomFactor_ = 1;
};

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

////////////////////////////////////////////////////////////////////////////////
//
/// Фикстура контроллера упаковки
/**
	*/
////////////////////////////////////////////////////////////////////////////////
struct ControllerFixture
{
  std::shared_ptr<StatisticsViewStub> view = std::make_shared<StatisticsViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(5, 5);
  std::shared_ptr<PackingController> controller = std::make_shared<PackingController>(view, selection, mainBoard, genBoard);
};

////////////////////////////////////////////////////////////////////////////////
//
/// Фикстура состояния упаковки
/**
	*/
////////////////////////////////////////////////////////////////////////////////
struct StateFixture
{
  std::shared_ptr<StatisticsViewStub> view = std::make_shared<StatisticsViewStub>();
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  std::shared_ptr<Board> mainBoard = Board::create(20, 20);
  std::shared_ptr<Board> genBoard = Board::create(5, 5);
  std::shared_ptr<PackingController> controller = std::make_shared<PackingController>(view, selection, mainBoard, genBoard);
  std::shared_ptr<PackagingState> state = std::make_shared<PackagingState>(controller);
};

// Создать событие нажатия мыши
SceneMouseEvent mousePress()
{
  return SceneMouseEvent(EventType::MousePress, Point2D{0, 0}, MouseButton::Left);
}

std::string readTextFile(const std::filesystem::path & path)
{
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Загрузить пул и активировать первую фигуру
std::shared_ptr<Figure> acquireActiveFigure(ControllerFixture & fixture)
{
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}, {1, 1}, {2, 1}});
  std::shared_ptr<Figure> nextFigure = makeFigure({{3, 0}, {4, 0}, {4, 1}, {5, 1}});
  fixture.controller->loadPool({figure, nextFigure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  EXPECT_NE(command, nullptr);
  command->execute();
  return fixture.mainBoard->getFigures().front();
}
} // namespace

//------------------------------------------------------------------------------
/**
  Проверяет отсутствие команды без активной фигуры
*/
//--
TEST(PackingActionMapper, WithoutActiveFigureReturnsNullptr)
{
  PackingActionMapper mapper;

  EXPECT_EQ(mapper.commandFor(Action::Left, nullptr), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет маппинг действий перемещения и поворота в команды
*/
//--
TEST(PackingActionMapper, MapsMoveAndRotateActionsToCommands)
{
  PackingActionMapper mapper;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});

  std::unique_ptr<ICommand> moveCommand = mapper.commandFor(Action::Up, figure);
  std::unique_ptr<ICommand> rotateCommand = mapper.commandFor(Action::Rotate, figure);

  EXPECT_NE(dynamic_cast<MoveCommand *>(moveCommand.get()), nullptr);
  EXPECT_NE(dynamic_cast<RotateCommand *>(rotateCommand.get()), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет показ preview-фигуры на доске генерации
*/
//--
TEST(PreviewPoolPresenter, ShowsPreviewOnGeneratorBoard)
{
  std::shared_ptr<Board> genBoard = Board::create(5, 5);
  PreviewPoolPresenter presenter(genBoard);
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}});

  presenter.showPreview(figure);

  EXPECT_EQ(genBoard->getFigures().size(), 1);
  EXPECT_EQ(genBoard->getFigures().front(), figure);
}


//------------------------------------------------------------------------------
/**
  Проверяет очистку доски генерации при отсутствии preview-фигуры
*/
//--
TEST(PreviewPoolPresenter, ClearsGeneratorBoardWithoutPreview)
{
  std::shared_ptr<Board> genBoard = Board::create(5, 5);
  PreviewPoolPresenter presenter(genBoard);
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}});

  presenter.showPreview(figure);
  presenter.showPreview(nullptr);

  EXPECT_TRUE(genBoard->getFigures().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет валидное размещение фигуры внутри доски
*/
//--
TEST(PlacementValidator, AcceptsFigureInsideBoard)
{
  PlacementValidator validator;
  std::shared_ptr<Board> board = Board::create(10, 10);
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{3, 3}});

  EXPECT_TRUE(validator.canPlace(board, figure));
  const PlacementValidationResult result = validator.validate(board, figure);
  EXPECT_EQ(result.code, PlacementValidationCode::Ok);
  EXPECT_TRUE(result.canPlace());
  EXPECT_EQ(result.message(), "Размещение возможно");
}


//------------------------------------------------------------------------------
/**
  Проверяет отклонение недоступной доски
*/
//--
TEST(PlacementValidator, RejectsInvalidBoardWithReason)
{
  PlacementValidator validator;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{3, 3}});

  const PlacementValidationResult result = validator.validate(nullptr, figure);

  EXPECT_EQ(result.code, PlacementValidationCode::InvalidBoard);
  EXPECT_FALSE(result.canPlace());
  EXPECT_EQ(result.message(), "Доска упаковки недоступна");
}


//------------------------------------------------------------------------------
/**
  Проверяет отклонение недоступной фигуры
*/
//--
TEST(PlacementValidator, RejectsInvalidFigureWithReason)
{
  PlacementValidator validator;
  std::shared_ptr<Board> board = Board::create(10, 10);

  const PlacementValidationResult result = validator.validate(board, nullptr);

  EXPECT_EQ(result.code, PlacementValidationCode::InvalidFigure);
  EXPECT_FALSE(result.canPlace());
  EXPECT_EQ(result.message(), "Фигура недоступна");
}


//------------------------------------------------------------------------------
/**
  Проверяет отклонение фигуры за границей доски
*/
//--
TEST(PlacementValidator, RejectsFigureOutsideBoard)
{
  PlacementValidator validator;
  std::shared_ptr<Board> board = Board::create(10, 10);
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{11, 3}});

  EXPECT_FALSE(validator.canPlace(board, figure));
  const PlacementValidationResult result = validator.validate(board, figure);
  EXPECT_EQ(result.code, PlacementValidationCode::OutOfBounds);
  EXPECT_FALSE(result.canPlace());
  EXPECT_EQ(result.message(), "Фигура выходит за границы доски");
}


//------------------------------------------------------------------------------
/**
  Проверяет отклонение пересечения с другой фигурой
*/
//--
TEST(PlacementValidator, RejectsIntersectionWithAnotherFigure)
{
  PlacementValidator validator;
  std::shared_ptr<Board> board = Board::create(10, 10);
  std::shared_ptr<Figure> placedFigure = makeFigureAt(Point2D{0, 0}, {{3, 3}});
  std::shared_ptr<Figure> candidateFigure = makeFigureAt(Point2D{0, 0}, {{3, 3}});

  board->add(placedFigure);

  EXPECT_FALSE(validator.canPlace(board, candidateFigure));
  const PlacementValidationResult result = validator.validate(board, candidateFigure);
  EXPECT_EQ(result.code, PlacementValidationCode::Intersection);
  EXPECT_FALSE(result.canPlace());
  EXPECT_EQ(result.message(), "Фигура пересекается с уже размещённой фигурой");
}

//------------------------------------------------------------------------------
/**
  Проверяет режим упаковки по умолчанию
*/
//--
TEST(PlacementValidator, RejectsEmptyCandidateFigure)
{
  PlacementValidator validator;
  std::shared_ptr<Board> board = Board::create(10, 10);
  std::shared_ptr<Figure> emptyFigure = std::make_shared<ObjectFigure>(Point2D{0, 0}, std::vector<Cell2D>{});

  EXPECT_FALSE(validator.canPlace(board, emptyFigure));
  const PlacementValidationResult result = validator.validate(board, emptyFigure);
  EXPECT_EQ(result.code, PlacementValidationCode::EmptyFigure);
  EXPECT_FALSE(result.canPlace());
  EXPECT_EQ(result.message(), "Фигура не содержит клеток");
}


//------------------------------------------------------------------------------
/**
  Проверяет успешный результат автоматической стратегии для валидной preview-фигуры
*/
//--
TEST(PreviewAutomaticPlacementStrategy, ValidPreviewReturnsCommandAndOk)
{
  ControllerFixture fixture;
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(fixture.genBoard);
  PreviewAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}});
  figurePool.load({figure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result =
    strategy.createPlacement(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  EXPECT_NE(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::Ok);
  EXPECT_TRUE(result.validation.canPlace());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ автоматической стратегии при выходе preview-фигуры за границы доски
*/
//--
TEST(PreviewAutomaticPlacementStrategy, OutOfBoundsPreviewReturnsFailure)
{
  ControllerFixture fixture;
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(fixture.genBoard);
  PreviewAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{30, 30}});
  figurePool.load({figure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result =
    strategy.createPlacement(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  EXPECT_EQ(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::OutOfBounds);
  EXPECT_TRUE(result.hasFailure());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ автоматической стратегии при пересечении с уже размещённой фигурой
*/
//--
TEST(PreviewAutomaticPlacementStrategy, IntersectingPreviewReturnsFailure)
{
  ControllerFixture fixture;
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(fixture.genBoard);
  PreviewAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> placedFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> previewFigure = makeFigure({{0, 0}});
  fixture.mainBoard->add(placedFigure);
  figurePool.load({previewFigure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result =
    strategy.createPlacement(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  EXPECT_EQ(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::Intersection);
  EXPECT_TRUE(result.hasFailure());
}


//------------------------------------------------------------------------------
/**
  Checks first-fit placement at board origin
*/
//--
TEST(FirstFitAutomaticPlacementStrategy, PlacesPreviewAtOriginOnEmptyBoard)
{
  ControllerFixture fixture;
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(fixture.genBoard);
  FirstFitAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> figure = makeFigure({{5, 5}});
  figurePool.load({figure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result =
    strategy.createPlacement(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  ASSERT_NE(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::Ok);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_EQ(figurePool.snapshotRemaining().size(), 1);
  EXPECT_EQ(figurePool.previewFigure(), figure);

  result.command->execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> placedFigure = fixture.mainBoard->getFigures().front();
  ASSERT_EQ(placedFigure->GetCells().size(), 1);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().column, 0);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().row, 0);
  EXPECT_TRUE(figurePool.snapshotRemaining().empty());
}


TEST(FirstFitAutomaticPlacementStrategy, SkipsOccupiedCellsAndPlacesAtNextValidPosition)
{
  ControllerFixture fixture;
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(fixture.genBoard);
  FirstFitAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> occupiedFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> previewFigure = makeFigure({{10, 10}});
  fixture.mainBoard->add(occupiedFigure);
  figurePool.load({previewFigure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result =
    strategy.createPlacement(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  ASSERT_NE(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::Ok);
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);

  result.command->execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 2);
  const std::vector<std::shared_ptr<Figure>> figures = fixture.mainBoard->getFigures();
  const std::shared_ptr<Figure> placedFigure = figures.front() == occupiedFigure ? figures.back() : figures.front();
  ASSERT_EQ(placedFigure->GetCells().size(), 1);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().column, 1);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().row, 0);
}


TEST(FirstFitAutomaticPlacementStrategy, ReturnsFailureWhenBoardHasNoValidPlace)
{
  std::shared_ptr<Board> board = Board::create(1, 1);
  std::shared_ptr<Board> genBoard = Board::create(5, 5);
  std::shared_ptr<SelectionModel> selection = std::make_shared<SelectionModel>();
  FigurePool figurePool(42);
  PreviewPoolPresenter presenter(genBoard);
  FirstFitAutomaticPlacementStrategy strategy;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> occupiedFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> previewFigure = makeFigure({{0, 0}});
  ASSERT_TRUE(board->add(occupiedFigure));
  figurePool.load({previewFigure});
  presenter.showPreview(figurePool.previewFigure());

  AutomaticPlacementResult result = strategy.createPlacement(board, figurePool, presenter, selection, &activeFigure);

  EXPECT_EQ(result.command, nullptr);
  EXPECT_EQ(result.validation.code, PlacementValidationCode::NoPlacementFound);
  EXPECT_TRUE(result.hasFailure());
  ASSERT_EQ(board->getFigures().size(), 1);
  EXPECT_EQ(board->getFigures().front(), occupiedFigure);
  ASSERT_EQ(figurePool.snapshotRemaining().size(), 1);
  EXPECT_EQ(figurePool.previewFigure(), previewFigure);
}


TEST(AutomaticPackPlanner, PlansAllFiguresWithoutMutatingBoardOrPool)
{
  ControllerFixture fixture;
  AutomaticPackPlanner planner;
  FirstFitAutomaticPlacementStrategy strategy;
  FigurePool figurePool(42);
  std::shared_ptr<Figure> first = makeFigure({{5, 5}});
  std::shared_ptr<Figure> second = makeFigure({{10, 10}});
  figurePool.load({first, second});
  const std::vector<std::shared_ptr<Figure>> before = figurePool.snapshotRemaining();

  AutomaticPackPlan plan = planner.createPlan(fixture.mainBoard, figurePool, strategy);

  ASSERT_TRUE(plan.success());
  EXPECT_EQ(plan.placements.size(), 2);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  EXPECT_EQ(figurePool.snapshotRemaining(), before);
}


TEST(AutomaticPackPlanner, RejectsInvalidCurrentScene)
{
  ControllerFixture fixture;
  AutomaticPackPlanner planner;
  FirstFitAutomaticPlacementStrategy strategy;
  FigurePool figurePool(42);
  std::shared_ptr<Figure> invalidFigure = makeFigureAt(Point2D{0, 0}, {{30, 30}});
  fixture.mainBoard->add(invalidFigure);
  figurePool.load({makeFigure({{0, 0}})});

  AutomaticPackPlan plan = planner.createPlan(fixture.mainBoard, figurePool, strategy);

  EXPECT_FALSE(plan.success());
  EXPECT_EQ(plan.validation.code, PlacementValidationCode::OutOfBounds);
  EXPECT_TRUE(plan.placements.empty());
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.mainBoard->getFigures().front(), invalidFigure);
  EXPECT_EQ(figurePool.snapshotRemaining().size(), 1);
}


TEST(AutomaticPackPlanner, FailureAfterPartialPlanningReturnsNoPartialPlan)
{
  ControllerFixture fixture;
  AutomaticPackPlanner planner;
  FirstFitAutomaticPlacementStrategy strategy;
  FigurePool figurePool(42);
  std::shared_ptr<Figure> placeable = makeFigure({{0, 0}});
  std::shared_ptr<Figure> impossible = makeFigure({{0, 0}, {20, 0}});
  figurePool.load({impossible});
  figurePool.returnAsPreview(placeable);
  const std::vector<std::shared_ptr<Figure>> before = figurePool.snapshotRemaining();

  AutomaticPackPlan plan = planner.createPlan(fixture.mainBoard, figurePool, strategy);

  EXPECT_FALSE(plan.success());
  EXPECT_EQ(plan.validation.code, PlacementValidationCode::NoPlacementFound);
  EXPECT_TRUE(plan.placements.empty());
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  EXPECT_EQ(figurePool.snapshotRemaining(), before);
}


TEST(AutoPackCommand, ExecuteUndoRedoTreatsAutoPackAsSingleCommand)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  FigurePool figurePool(42);
  std::shared_ptr<Figure> firstPreview = makeFigure({{0, 0}});
  std::shared_ptr<Figure> secondPreview = makeFigure({{1, 0}});
  figurePool.load({secondPreview});
  figurePool.returnAsPreview(firstPreview);
  presenter.showPreview(figurePool.previewFigure());
  std::shared_ptr<Figure> activeFigure;

  std::vector<std::unique_ptr<PlacePreviewFigureCommand>> commands;
  commands.push_back(std::make_unique<PlacePreviewFigureCommand>(fixture.mainBoard, figurePool, presenter, fixture.selection,
                                                                 &activeFigure, makeFigureAt(Point2D{0, 0}, {{0, 0}}),
                                                                 PreviewConsumptionPolicy::ConsumePreview));
  commands.push_back(std::make_unique<PlacePreviewFigureCommand>(fixture.mainBoard, figurePool, presenter, fixture.selection,
                                                                 &activeFigure, makeFigureAt(Point2D{0, 0}, {{1, 0}}),
                                                                 PreviewConsumptionPolicy::ConsumePreview));
  AutoPackCommand command(std::move(commands));

  EXPECT_EQ(command.placedCount(), 2);
  command.execute();
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 2);
  EXPECT_TRUE(figurePool.snapshotRemaining().empty());

  command.undo();
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_EQ(figurePool.snapshotRemaining().size(), 2);
  EXPECT_EQ(figurePool.snapshotRemaining().front(), firstPreview);

  command.execute();
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 2);
  EXPECT_TRUE(figurePool.snapshotRemaining().empty());
}


//------------------------------------------------------------------------------
/**
  Checks default packing mode
*/
//--
TEST(PackingController, DefaultsToManualMode)
{
  ControllerFixture fixture;

  EXPECT_EQ(fixture.controller->mode(), PackingMode::Manual);
}


//------------------------------------------------------------------------------
/**
  Проверяет нормализацию геометрии и placement в snapshot сцены упаковки
*/
//--
TEST(PackingController, PackingSceneSnapshotNormalizesGeometryAndPlacement)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{5, 3}, {6, 3}, {6, 4}});
  fixture.mainBoard->add(figure);

  const PackingSceneSnapshot snapshot = fixture.controller->packingSceneSnapshot();

  ASSERT_EQ(snapshot.board.columns, 20);
  ASSERT_EQ(snapshot.board.rows, 20);
  ASSERT_EQ(snapshot.objects.size(), 1);
  ASSERT_EQ(snapshot.actions.size(), 1);
  EXPECT_EQ(snapshot.objects.front().id, "object_0");
  ASSERT_EQ(snapshot.objects.front().cells.size(), 3);
  EXPECT_EQ(snapshot.objects.front().cells[0].column, 0);
  EXPECT_EQ(snapshot.objects.front().cells[0].row, 0);
  EXPECT_EQ(snapshot.objects.front().cells[1].column, 1);
  EXPECT_EQ(snapshot.objects.front().cells[1].row, 0);
  EXPECT_EQ(snapshot.objects.front().cells[2].column, 1);
  EXPECT_EQ(snapshot.objects.front().cells[2].row, 1);
  EXPECT_EQ(snapshot.actions.front().objectId, "object_0");
  EXPECT_DOUBLE_EQ(snapshot.actions.front().x, 5.0);
  EXPECT_DOUBLE_EQ(snapshot.actions.front().y, 3.0);
  EXPECT_DOUBLE_EQ(snapshot.actions.front().rotationDegrees, 0.0);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что snapshot сцены не включает оставшийся пул
*/
//--
TEST(PackingController, PackingSceneSnapshotDoesNotIncludeRemainingPool)
{
  ControllerFixture fixture;
  fixture.controller->loadPool({makeFigure({{0, 0}})});

  const PackingSceneSnapshot snapshot = fixture.controller->packingSceneSnapshot();

  EXPECT_TRUE(snapshot.objects.empty());
  EXPECT_TRUE(snapshot.actions.empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет загрузку валидной сцены с заменой текущей packing-сессии
*/
//--
TEST(PackingController, LoadSceneSnapshotReplacesBoardAndClearsPoolActiveSelection)
{
  ControllerFixture fixture;
  fixture.mainBoard->add(makeFigureAt(Point2D{0, 0}, {{10, 10}}));
  fixture.controller->loadPool({makeFigure({{0, 0}})});

  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}, {1, 0}}});
  snapshot.actions.push_back({"place", "object_0", 3.0, 4.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  ASSERT_TRUE(result.success);
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(fixture.genBoard->getFigures().empty());
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
  EXPECT_EQ(fixture.controller->onEvent(ActionEvent(Action::Right)), nullptr);
  EXPECT_FALSE(fixture.selection->findFigure(fixture.mainBoard->getFigures().front()));
  const Coordinates firstCell = fixture.mainBoard->getFigures().front()->GetCells().front().GetCoordinates();
  EXPECT_EQ(firstCell.column, 3);
  EXPECT_EQ(firstCell.row, 4);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что невалидная сцена не меняет текущую доску
*/
//--
TEST(PackingController, LoadSceneSnapshotFailureDoesNotMutateBoard)
{
  ControllerFixture fixture;
  fixture.mainBoard->add(makeFigureAt(Point2D{0, 0}, {{5, 5}}));

  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 20.0, 0.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  ASSERT_FALSE(result.success);
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const Coordinates coords = fixture.mainBoard->getFigures().front()->GetCells().front().GetCoordinates();
  EXPECT_EQ(coords.column, 5);
  EXPECT_EQ(coords.row, 5);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при несовпадении размера доски
*/
//--
TEST(PackingController, LoadSceneSnapshotRejectsUnsupportedBoardSize)
{
  ControllerFixture fixture;
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 10;
  snapshot.board.rows = 10;

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  EXPECT_FALSE(result.success);
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при пересечении фигур в загружаемой сцене
*/
//--
TEST(PackingController, LoadSceneSnapshotRejectsIntersectingScene)
{
  ControllerFixture fixture;
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.objects.push_back({"object_1", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.0, 0.0, 0.0});
  snapshot.actions.push_back({"place", "object_1", 0.0, 0.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при дробных координатах действия snapshot
*/
//--
TEST(PackingController, LoadSceneSnapshotRejectsFractionalActionCoordinates)
{
  ControllerFixture fixture;
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", 0.5, 0.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ при координатах действия snapshot вне диапазона int
*/
//--
TEST(PackingController, LoadSceneSnapshotRejectsOutOfRangeActionCoordinates)
{
  ControllerFixture fixture;
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}}});
  snapshot.actions.push_back({"place", "object_0", static_cast<double>(std::numeric_limits<int>::max()) + 1.0, 0.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ, если сумма локальной клетки и смещения выходит за диапазон int
*/
//--
TEST(PackingController, LoadSceneSnapshotRejectsCellOffsetOverflow)
{
  ControllerFixture fixture;
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{std::numeric_limits<int>::max(), 0}}});
  snapshot.actions.push_back({"place", "object_0", 1.0, 0.0, 0.0});

  const PackingSceneLoadResult result = fixture.controller->loadSceneSnapshot(snapshot);

  EXPECT_FALSE(result.success);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет обновление статистики после выполнения команд
*/
//--
TEST(MainController, UpdatesStatisticsAfterCommandExecutionUndoAndRedo)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_stats_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);

  EXPECT_EQ(view->occupiedCells_, 0);

  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  EXPECT_EQ(view->occupiedCells_, 1);

  controller.onEvent(UndoEvent());
  EXPECT_EQ(view->occupiedCells_, 0);

  controller.onEvent(RedoEvent());
  EXPECT_EQ(view->occupiedCells_, 1);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет открытие JSON-сцены через MainController
*/
//--
TEST(MainController, OpenSceneFileEventOpensJsonSceneAndReportsSuccess)
{
  const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "aipackaging_open_scene.json";
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}, {1, 0}}});
  snapshot.actions.push_back({"place", "object_0", 4.0, 5.0, 0.0});
  ASSERT_TRUE(PackingSceneSaver::saveToFile(scenePath.string(), snapshot));

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = scenePath.string();
  MainController controller(view);

  controller.onEvent(OpenSceneFileEvent());

  EXPECT_EQ(view->occupiedCells_, 2);
  EXPECT_EQ(view->redrawRequests_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Открытие сцены");
  EXPECT_EQ(view->lastMessageText_, "Сцена упаковки открыта");
  EXPECT_EQ(view->lastMessageType_, MessageType::Info);

  std::filesystem::remove(scenePath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что ошибка открытия JSON-сцены не очищает текущую сцену
*/
//--
TEST(MainController, OpenSceneFailurePreservesCurrentScene)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_open_scene_preserve_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);

  view->loadPath_ = (std::filesystem::temp_directory_path() / "aipackaging_missing_scene.json").string();
  controller.onEvent(OpenSceneFileEvent());

  EXPECT_EQ(view->occupiedCells_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Ошибка открытия сцены");
  EXPECT_EQ(view->lastMessageType_, MessageType::Error);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что открытие сцены сбрасывает историю команд
*/
//--
TEST(MainController, OpenSceneClearsUndoHistory)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_open_scene_history_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  const std::filesystem::path scenePath = std::filesystem::temp_directory_path() / "aipackaging_open_scene_history.json";
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = 20;
  snapshot.board.rows = 20;
  snapshot.objects.push_back({"object_0", {{0, 0}, {1, 0}}});
  snapshot.actions.push_back({"place", "object_0", 4.0, 5.0, 0.0});
  ASSERT_TRUE(PackingSceneSaver::saveToFile(scenePath.string(), snapshot));

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);

  view->loadPath_ = scenePath.string();
  controller.onEvent(OpenSceneFileEvent());
  ASSERT_EQ(view->occupiedCells_, 2);

  controller.onEvent(UndoEvent());

  EXPECT_EQ(view->occupiedCells_, 2);

  std::filesystem::remove(poolPath);
  std::filesystem::remove(scenePath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что загрузка OBJECT-пула не очищает текущую сцену
*/
//--
TEST(MainController, LoadFileEventStillLoadsPoolWithoutClearingMainScene)
{
  const std::filesystem::path firstPoolPath = std::filesystem::temp_directory_path() / "aipackaging_first_pool.txt";
  {
    std::ofstream output(firstPoolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  const std::filesystem::path secondPoolPath = std::filesystem::temp_directory_path() / "aipackaging_second_pool.txt";
  {
    std::ofstream output(secondPoolPath);
    output << "OBJECT\nCELL 2 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = firstPoolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);

  view->loadPath_ = secondPoolPath.string();
  controller.onEvent(LoadFileEvent());

  EXPECT_EQ(view->occupiedCells_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Загрузка");

  std::filesystem::remove(firstPoolPath);
  std::filesystem::remove(secondPoolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что загрузка OBJECT-пула сбрасывает историю команд старого пула
*/
//--
TEST(MainController, LoadPoolClearsUndoHistory)
{
  const std::filesystem::path firstPoolPath = std::filesystem::temp_directory_path() / "aipackaging_history_first_pool.txt";
  {
    std::ofstream output(firstPoolPath);
    output << "OBJECT\nCELL 10 10\nEND_OBJECT\n";
  }

  const std::filesystem::path secondPoolPath = std::filesystem::temp_directory_path() / "aipackaging_history_second_pool.txt";
  {
    std::ofstream output(secondPoolPath);
    output << "OBJECT\nCELL 0 0\nCELL 1 0\nCELL 2 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = firstPoolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);

  view->loadPath_ = secondPoolPath.string();
  controller.onEvent(LoadFileEvent());
  controller.onEvent(UndoEvent());
  controller.onEvent(mousePress());

  EXPECT_EQ(view->occupiedCells_, 4);

  std::filesystem::remove(firstPoolPath);
  std::filesystem::remove(secondPoolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет обновление статистики и перерисовку после удаления активной фигуры
*/
//--
TEST(MainController, DeleteUpdatesStatisticsAndRequestsRedraw)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_delete_stats_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
    output << "OBJECT\nCELL 1 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);
  const size_t redrawBeforeDelete = view->redrawRequests_;
  const size_t messagesBeforeDelete = view->messageCount_;

  controller.onEvent(ActionEvent(Action::Delete));

  EXPECT_EQ(view->occupiedCells_, 0);
  EXPECT_GT(view->redrawRequests_, redrawBeforeDelete);
  EXPECT_EQ(view->messageCount_, messagesBeforeDelete);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет сообщение об ошибке загрузки при неверном пути
*/
//--
TEST(MainController, ReloadPoolAllowsAcquireAfterStaleInvalidActiveFigure)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_reload_active_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  ASSERT_EQ(view->occupiedCells_, 1);

  controller.onEvent(ActionEvent(Action::Left));
  controller.onEvent(ActionEvent(Action::Left));
  controller.onEvent(ActionEvent(Action::Left));
  ASSERT_EQ(view->lastMessageTitle_, "Размещение невалидно");
  controller.onEvent(mousePress());
  ASSERT_EQ(view->lastMessageTitle_, "Размещение невозможно");
  const size_t occupiedAfterBlockedAcquire = view->occupiedCells_;

  controller.onEvent(LoadFileEvent());
  const size_t redrawAfterReload = view->redrawRequests_;
  controller.onEvent(mousePress());

  EXPECT_GT(view->occupiedCells_, occupiedAfterBlockedAcquire);
  EXPECT_GT(view->redrawRequests_, redrawAfterReload);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет сообщение об ошибке загрузки при неверном пути
*/
//--
TEST(MainController, LoadShowsErrorMessageForInvalidPath)
{
  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = (std::filesystem::temp_directory_path() / "aipackaging_missing_pool.txt").string();
  MainController controller(view);

  controller.onEvent(LoadFileEvent());

  EXPECT_EQ(view->messageCount_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Ошибка загрузки пула");
  EXPECT_EQ(view->lastMessageType_, MessageType::Error);
  EXPECT_EQ(view->lastMessageText_, "Не удалось открыть файл пула фигур");
}


//------------------------------------------------------------------------------
/**
  Проверяет сообщение об успешной загрузке пула
*/
//--
TEST(MainController, LoadShowsInfoMessageForValidPool)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_load_message_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);

  controller.onEvent(LoadFileEvent());

  EXPECT_EQ(view->messageCount_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Загрузка");
  EXPECT_EQ(view->lastMessageText_, "Пул фигур загружен");
  EXPECT_EQ(view->lastMessageType_, MessageType::Info);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет сообщение об ошибке сохранения при неверном пути
*/
//--
TEST(MainController, SaveShowsErrorMessageForInvalidPath)
{
  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->savePath_ = (std::filesystem::temp_directory_path() / "aipackaging_missing_dir" / "scene.json").string();
  MainController controller(view);

  controller.onEvent(SaveFileEvent());

  EXPECT_EQ(view->messageCount_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Ошибка сохранения");
  EXPECT_EQ(view->lastMessageText_, "Не удалось сохранить сцену упаковки");
  EXPECT_EQ(view->lastMessageType_, MessageType::Error);
}


//------------------------------------------------------------------------------
/**
  Проверяет сообщение об успешном сохранении пула
*/
//--
TEST(MainController, SaveShowsInfoMessageForValidPath)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_save_message_scene.json";
  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->savePath_ = poolPath.string();
  MainController controller(view);

  controller.onEvent(SaveFileEvent());

  EXPECT_EQ(view->messageCount_, 1);
  EXPECT_EQ(view->lastMessageTitle_, "Сохранение");
  EXPECT_EQ(view->lastMessageText_, "Сцена упаковки сохранена");
  EXPECT_EQ(view->lastMessageType_, MessageType::Info);
  EXPECT_TRUE(std::filesystem::exists(poolPath));

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что Save сохраняет JSON сцены упаковки, а не OBJECT пул
*/
//--
TEST(MainController, SaveWritesPlacedSceneAsJson)
{
  const std::filesystem::path loadPath = std::filesystem::temp_directory_path() / "aipackaging_scene_save_pool.txt";
  const std::filesystem::path savePath = std::filesystem::temp_directory_path() / "aipackaging_scene_save.json";
  {
    std::ofstream output(loadPath);
    output << "OBJECT\nCELL 5 3\nCELL 6 3\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = loadPath.string();
  view->savePath_ = savePath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());

  controller.onEvent(SaveFileEvent());

  const std::string text = readTextFile(savePath);
  EXPECT_NE(text.find("\"format\": \"aipackaging.packing_scene\""), std::string::npos);
  EXPECT_NE(text.find("\"objects\": ["), std::string::npos);
  EXPECT_NE(text.find("\"actions\": ["), std::string::npos);
  EXPECT_NE(text.find("\"x\": 5.0"), std::string::npos);
  EXPECT_NE(text.find("\"y\": 3.0"), std::string::npos);
  EXPECT_NE(text.find("{ \"column\": 0, \"row\": 0 }"), std::string::npos);
  EXPECT_NE(text.find("{ \"column\": 1, \"row\": 0 }"), std::string::npos);
  EXPECT_EQ(text.find("OBJECT\n"), std::string::npos);

  std::filesystem::remove(loadPath);
  std::filesystem::remove(savePath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что Save не сохраняет невалидную сцену
*/
//--
TEST(MainController, SaveRejectsInvalidPackingScene)
{
  const std::filesystem::path loadPath = std::filesystem::temp_directory_path() / "aipackaging_invalid_scene_pool.txt";
  const std::filesystem::path savePath = std::filesystem::temp_directory_path() / "aipackaging_invalid_scene.json";
  std::filesystem::remove(savePath);
  {
    std::ofstream output(loadPath);
    output << "OBJECT\nCELL 30 30\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = loadPath.string();
  view->savePath_ = savePath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());

  controller.onEvent(SaveFileEvent());

  EXPECT_EQ(view->lastMessageTitle_, "Ошибка сохранения");
  EXPECT_EQ(view->lastMessageText_, "Нельзя сохранить невалидную расстановку");
  EXPECT_EQ(view->lastMessageType_, MessageType::Error);
  EXPECT_FALSE(std::filesystem::exists(savePath));

  std::filesystem::remove(loadPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что Save после Delete не сохраняет возвращённую preview-фигуру как размещённую
*/
//--
TEST(MainController, SaveAfterDeleteDoesNotPersistReturnedPreview)
{
  const std::filesystem::path loadPath = std::filesystem::temp_directory_path() / "aipackaging_delete_scene_pool.txt";
  const std::filesystem::path savePath = std::filesystem::temp_directory_path() / "aipackaging_delete_scene.json";
  {
    std::ofstream output(loadPath);
    output << "OBJECT\nCELL 0 0\nEND_OBJECT\n";
    output << "OBJECT\nCELL 1 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = loadPath.string();
  view->savePath_ = savePath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());
  controller.onEvent(ActionEvent(Action::Delete));

  controller.onEvent(SaveFileEvent());

  const std::string text = readTextFile(savePath);
  EXPECT_NE(text.find("\"objects\": [\n  ],"), std::string::npos);
  EXPECT_NE(text.find("\"actions\": [\n  ]"), std::string::npos);
  EXPECT_EQ(text.find("\"id\": \"object_0\""), std::string::npos);

  std::filesystem::remove(loadPath);
  std::filesystem::remove(savePath);
}


//------------------------------------------------------------------------------
/**
  Checks command-driven AutoPlace statistics and history flow
*/
//--
TEST(MainController, AutoPlaceActionUpdatesStatisticsAndSupportsUndoRedo)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_autoplace_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 5 5\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  const size_t redrawAfterLoad = view->redrawRequests_;

  controller.onEvent(ActionEvent(Action::AutoPlace));

  EXPECT_EQ(view->occupiedCells_, 1);
  EXPECT_GT(view->redrawRequests_, redrawAfterLoad);

  controller.onEvent(UndoEvent());
  EXPECT_EQ(view->occupiedCells_, 0);

  controller.onEvent(RedoEvent());
  EXPECT_EQ(view->occupiedCells_, 1);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет warning-сообщение при невозможности автоматического размещения
*/
//--
TEST(MainController, AutoPackAllUpdatesStatisticsShowsSummaryAndSupportsSingleUndo)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_autopack_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 5 5\nEND_OBJECT\n";
    output << "OBJECT\nCELL 10 10\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  const size_t redrawAfterLoad = view->redrawRequests_;

  controller.onEvent(ActionEvent(Action::AutoPackAll));

  EXPECT_EQ(view->occupiedCells_, 2);
  EXPECT_GT(view->redrawRequests_, redrawAfterLoad);
  EXPECT_EQ(view->lastMessageTitle_, "Автоупаковка");
  EXPECT_EQ(view->lastMessageText_, "Размещено фигур: 2");
  EXPECT_EQ(view->lastMessageType_, MessageType::Info);

  controller.onEvent(UndoEvent());
  EXPECT_EQ(view->occupiedCells_, 0);

  controller.onEvent(RedoEvent());
  EXPECT_EQ(view->occupiedCells_, 2);

  std::filesystem::remove(poolPath);
}


TEST(MainController, AutoPackAllFailureShowsWarningAndPreservesScene)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_autopack_failure_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nCELL 20 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());

  controller.onEvent(ActionEvent(Action::AutoPackAll));

  EXPECT_EQ(view->occupiedCells_, 0);
  EXPECT_EQ(view->messageCount_, 2);
  EXPECT_EQ(view->lastMessageTitle_, "Размещение невозможно");
  EXPECT_EQ(view->lastMessageText_, "Не найдено место для автоматического размещения фигуры");
  EXPECT_EQ(view->lastMessageType_, MessageType::Warning);

  std::filesystem::remove(poolPath);
}


TEST(MainController, AutomaticFailureShowsWarningMessage)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_auto_failure_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 0 0\nCELL 20 0\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(ActionEvent(Action::AutoPlace));

  EXPECT_EQ(view->messageCount_, 2);
  EXPECT_EQ(view->lastMessageTitle_, "Размещение невозможно");
  EXPECT_EQ(view->lastMessageText_, "Не найдено место для автоматического размещения фигуры");
  EXPECT_EQ(view->lastMessageType_, MessageType::Warning);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет warning после ручной команды, которая оставляет активную фигуру в невалидной позиции
*/
//--
TEST(MainController, ManualInvalidPlacementShowsWarningMessage)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_manual_invalid_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 30 30\nEND_OBJECT\n";
    output << "OBJECT\nCELL 30 30\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());

  controller.onEvent(mousePress());

  EXPECT_EQ(view->messageCount_, 2);
  EXPECT_EQ(view->lastMessageTitle_, "Размещение невалидно");
  EXPECT_EQ(view->lastMessageText_, "Фигура выходит за границы доски");
  EXPECT_EQ(view->lastMessageType_, MessageType::Warning);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет warning при попытке взять следующую фигуру, пока активная фигура невалидна
*/
//--
TEST(MainController, BlockedManualAcquireShowsWarningMessage)
{
  const std::filesystem::path poolPath = std::filesystem::temp_directory_path() / "aipackaging_blocked_manual_acquire_pool.txt";
  {
    std::ofstream output(poolPath);
    output << "OBJECT\nCELL 30 30\nEND_OBJECT\n";
    output << "OBJECT\nCELL 30 30\nEND_OBJECT\n";
  }

  std::shared_ptr<MainViewStub> view = std::make_shared<MainViewStub>();
  view->loadPath_ = poolPath.string();
  MainController controller(view);
  controller.onEvent(LoadFileEvent());
  controller.onEvent(mousePress());

  controller.onEvent(mousePress());

  EXPECT_EQ(view->messageCount_, 3);
  EXPECT_EQ(view->lastMessageTitle_, "Размещение невозможно");
  EXPECT_EQ(view->lastMessageText_, "Фигура выходит за границы доски");
  EXPECT_EQ(view->lastMessageType_, MessageType::Warning);

  std::filesystem::remove(poolPath);
}


//------------------------------------------------------------------------------
/**
  Проверяет появление preview после загрузки пула
*/
//--
TEST(PackingController, LoadPoolShowsPreviewOnGeneratorBoard)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});

  fixture.controller->loadPool({figure});

  EXPECT_NE(fixture.genBoard->findFigure({5, -5}, 2), nullptr);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет получение фигуры со сцены генерации
*/
//--
TEST(PackingController, MousePressWithPreviewReturnsCreateCommandForMainBoard)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}, {1, 1}, {2, 1}});
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(command.get()), nullptr);
  EXPECT_NE(fixture.genBoard->findFigure({5, -5}, 2), nullptr);
  EXPECT_FALSE(fixture.selection->findFigure(figure));

  command->execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_NE(fixture.mainBoard->getFigures().front(), figure);
  EXPECT_EQ(fixture.mainBoard->getFigures().front()->topLeft().x, 0);
  EXPECT_EQ(fixture.mainBoard->getFigures().front()->topLeft().y, 0);
  EXPECT_EQ(fixture.genBoard->findFigure({5, -5}, 2), nullptr);
  EXPECT_FALSE(fixture.selection->findFigure(figure));
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие fallback при пустом пуле
*/
//--
TEST(PackingController, UndoRestoresPreviewPoolStateAfterAcquire)
{
  ControllerFixture fixture;
  CommandManager commandManager;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});

  fixture.controller->loadPool({figure});
  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  ASSERT_NE(command, nullptr);

  commandManager.execute(std::move(command));
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
  EXPECT_FALSE(fixture.selection->findFigure(figure));

  commandManager.undo();
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().front(), figure);
  EXPECT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().front(), figure);
  EXPECT_FALSE(fixture.selection->findFigure(figure));

  commandManager.redo();
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
  EXPECT_TRUE(fixture.genBoard->getFigures().empty());
  EXPECT_FALSE(fixture.selection->findFigure(figure));
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие fallback при пустом пуле
*/
//--
TEST(PackingController, MousePressWithoutPoolReturnsNullptr)
{
  ControllerFixture fixture;

  EXPECT_EQ(fixture.controller->onEvent(mousePress()), nullptr);
  EXPECT_EQ(fixture.genBoard->findFigure({5, -5}, 2), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет появление следующей preview после выдачи фигуры
*/
//--
TEST(PackingController, TakePreviewPromotesNextFigure)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigure({{0, 0}, {0, 1}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}, {2, 0}});

  fixture.controller->loadPool({first, second});
  const std::vector<std::shared_ptr<Figure>> before = fixture.controller->remainingPoolSnapshot();

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  ASSERT_NE(command, nullptr);
  command->execute();

  const std::vector<std::shared_ptr<Figure>> after = fixture.controller->remainingPoolSnapshot();
  EXPECT_EQ(before.size(), 2);
  EXPECT_EQ(after.size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().size(), 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет выделение активной фигуры, если в пуле осталась preview-фигура
*/
//--
TEST(PackingController, PlacementSelectsActiveFigureWhenPoolHasNextPreview)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigure({{0, 0}, {0, 1}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}, {2, 0}});
  fixture.controller->loadPool({first, second});
  const std::shared_ptr<Figure> firstPreview = fixture.controller->remainingPoolSnapshot().front();

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  ASSERT_NE(command, nullptr);
  command->execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> placedFigure = fixture.mainBoard->getFigures().front();
  EXPECT_NE(placedFigure, firstPreview);
  EXPECT_FALSE(fixture.selection->findFigure(firstPreview));
  EXPECT_TRUE(fixture.selection->findFigure(placedFigure));
  std::unique_ptr<ICommand> actionCommand = fixture.controller->onEvent(ActionEvent(Action::Right));
  EXPECT_NE(dynamic_cast<MoveCommand *>(actionCommand.get()), nullptr);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет сохранение текущего сценария получения preview в ручном режиме
*/
//--
TEST(PackingController, ManualModeKeepsMouseAcquireFlow)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});

  fixture.controller->setMode(PackingMode::Manual);
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(command.get()), nullptr);
  EXPECT_FALSE(fixture.selection->findFigure(figure));
  command->execute();
  EXPECT_FALSE(fixture.selection->findFigure(figure));
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет сброс активной фигуры и выделения при размещении последней фигуры
*/
//--
TEST(PackingController, LastPlacementClearsActiveFigureAndSelection)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  EXPECT_FALSE(fixture.selection->findFigure(figure));

  std::unique_ptr<ICommand> actionCommand = fixture.controller->onEvent(ActionEvent(Action::Right));
  EXPECT_EQ(actionCommand, nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет размещение единственной фигуры из пула через менеджер команд
*/
//--
TEST(PackingController, SingleFigurePoolPlacesLastFigureThroughCommandManager)
{
  ControllerFixture fixture;
  CommandManager commandManager;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  ASSERT_NE(command, nullptr);
  commandManager.execute(std::move(command));

  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_NE(fixture.mainBoard->getFigures().front(), figure);
  EXPECT_EQ(fixture.mainBoard->getFigures().front()->topLeft().x, 0);
  EXPECT_EQ(fixture.mainBoard->getFigures().front()->topLeft().y, 0);
  EXPECT_TRUE(fixture.genBoard->getFigures().empty());
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие активной фигуры после клика по пустому пулу
*/
//--
TEST(PackingController, EmptyPoolMousePressKeepsNoActiveFigure)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigure({{0, 0}, {1, 0}});
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();
  ASSERT_TRUE(fixture.controller->remainingPoolSnapshot().empty());

  EXPECT_EQ(fixture.controller->onEvent(mousePress()), nullptr);

  std::unique_ptr<ICommand> actionCommand = fixture.controller->onEvent(ActionEvent(Action::Right));
  EXPECT_EQ(actionCommand, nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет нормализацию preview-фигуры верхнего ряда при размещении на основной доске
*/
//--
TEST(PlacePreviewFigureCommand, ManualPlacementNormalizesLoadedTopRowFigure)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  std::shared_ptr<Figure> activeFigure;
  const auto loadResult = ObjectPoolLoader::loadFromText("OBJECT\nCELL 0 0\nEND_OBJECT\n");
  ASSERT_TRUE(loadResult.success);
  ASSERT_EQ(loadResult.figures.size(), 1);
  FigurePool figurePool;
  figurePool.load(loadResult.figures);
  const std::shared_ptr<Figure> previewFigure = figurePool.previewFigure();
  ASSERT_NE(previewFigure, nullptr);
  EXPECT_EQ(previewFigure->topLeft().x, 5);
  EXPECT_EQ(previewFigure->topLeft().y, -5);

  PlacePreviewFigureCommand command(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  command.execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> placedFigure = fixture.mainBoard->getFigures().front();
  EXPECT_NE(placedFigure, previewFigure);
  EXPECT_EQ(placedFigure->topLeft().x, 0);
  EXPECT_EQ(placedFigure->topLeft().y, 0);
  PlacementValidator validator;
  EXPECT_TRUE(validator.canPlace(fixture.mainBoard, placedFigure));
  EXPECT_EQ(validator.validate(fixture.mainBoard, placedFigure).code, PlacementValidationCode::Ok);
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие OutOfBounds у загруженной фигуры верхнего ряда после ручного размещения
*/
//--
TEST(PackingController, ManualPlacementOfLoadedTopRowDoesNotBecomeInvalid)
{
  ControllerFixture fixture;
  const auto loadResult = ObjectPoolLoader::loadFromText("OBJECT\nCELL 0 0\nEND_OBJECT\n"
                                                         "OBJECT\nCELL 1 0\nEND_OBJECT\n");
  ASSERT_TRUE(loadResult.success);
  ASSERT_EQ(loadResult.figures.size(), 2);
  fixture.controller->loadPool(loadResult.figures);

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  ASSERT_NE(command, nullptr);
  command->execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> placedFigure = fixture.mainBoard->getFigures().front();
  PlacementValidator validator;
  EXPECT_TRUE(validator.canPlace(fixture.mainBoard, placedFigure));

  fixture.controller->updateActivePlacementStatus();

  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());
  EXPECT_TRUE(fixture.selection->findFigure(placedFigure));
}


//------------------------------------------------------------------------------
/**
  Проверяет размещение последней фигуры после продвижения preview
*/
//--
TEST(PackingController, TwoFigurePoolPlacesLastFigureAndClearsGenerator)
{
  ControllerFixture fixture;
  CommandManager commandManager;
  std::shared_ptr<Figure> first = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> second = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  fixture.controller->loadPool({first, second});

  std::unique_ptr<ICommand> firstCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(firstCommand, nullptr);
  commandManager.execute(std::move(firstCommand));
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);

  std::unique_ptr<ICommand> secondCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(secondCommand, nullptr);
  commandManager.execute(std::move(secondCommand));

  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 2);
  EXPECT_TRUE(fixture.genBoard->getFigures().empty());
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
  EXPECT_EQ(fixture.controller->onEvent(mousePress()), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет размещение заранее подготовленной фигуры без preview в пуле
*/
//--
TEST(PlacePreviewFigureCommand, ExecutesPrebuiltPlacementWithoutPreview)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  FigurePool emptyPool;
  PlacePreviewFigureCommand command(fixture.mainBoard, emptyPool, presenter, fixture.selection, &activeFigure, figure);

  command.execute();

  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.mainBoard->getFigures().front(), figure);
  EXPECT_TRUE(emptyPool.snapshotRemaining().empty());
  EXPECT_TRUE(fixture.genBoard->getFigures().empty());
  EXPECT_FALSE(fixture.selection->findFigure(figure));
  EXPECT_EQ(activeFigure, nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что заранее подготовленная фигура по умолчанию не забирает preview из пула
*/
//--
TEST(PlacePreviewFigureCommand, PrebuiltPlacementPreservesPreviewByDefault)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> previewFigure = makeFigureAt(Point2D{5, -5}, {{0, 0}});
  std::shared_ptr<Figure> placedFigure = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  FigurePool figurePool;
  figurePool.load({previewFigure});

  PlacePreviewFigureCommand command(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure, placedFigure);

  command.execute();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.mainBoard->getFigures().front(), placedFigure);
  ASSERT_EQ(figurePool.snapshotRemaining().size(), 1);
  EXPECT_EQ(figurePool.snapshotRemaining().front(), previewFigure);
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().front(), previewFigure);
  EXPECT_EQ(activeFigure, placedFigure);
  EXPECT_TRUE(fixture.selection->findFigure(placedFigure));
}


//------------------------------------------------------------------------------
/**
  Проверяет явное потребление preview и восстановление состояния пула при undo/redo
*/
//--
TEST(PlacePreviewFigureCommand, ExplicitConsumePreviewRestoresPoolStateOnUndoRedo)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  CommandManager commandManager;
  std::shared_ptr<Figure> activeFigure;
  std::shared_ptr<Figure> previewFigure = makeFigureAt(Point2D{5, -5}, {{0, 0}});
  std::shared_ptr<Figure> placedFigure = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  FigurePool figurePool;
  figurePool.load({previewFigure});

  auto command =
    std::make_unique<PlacePreviewFigureCommand>(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure,
                                                placedFigure, PreviewConsumptionPolicy::ConsumePreview);

  commandManager.execute(std::move(command));
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(figurePool.snapshotRemaining().empty());

  commandManager.undo();
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_EQ(figurePool.snapshotRemaining().size(), 1);
  EXPECT_EQ(figurePool.snapshotRemaining().front(), previewFigure);

  commandManager.redo();
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(figurePool.snapshotRemaining().empty());
}


//------------------------------------------------------------------------------
/**
  Checks one-shot AutoPlace action from manual mode
*/
//--
TEST(PackingController, AutoPlaceActionReturnsPlacementCommandInManualMode)
{
  ControllerFixture fixture;
  fixture.controller->loadPool({makeFigure({{5, 5}})});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::AutoPlace));

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(command.get()), nullptr);
  command->execute();
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> placedFigure = fixture.mainBoard->getFigures().front();
  ASSERT_EQ(placedFigure->GetCells().size(), 1);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().column, 0);
  EXPECT_EQ(placedFigure->GetCells().front().GetCoordinates().row, 0);
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
}


TEST(PackingController, AutoPackAllActionReturnsAutoPackCommand)
{
  ControllerFixture fixture;
  fixture.controller->loadPool({makeFigure({{0, 0}}), makeFigure({{10, 10}})});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::AutoPackAll));

  AutoPackCommand * autoPackCommand = dynamic_cast<AutoPackCommand *>(command.get());
  ASSERT_NE(autoPackCommand, nullptr);
  EXPECT_EQ(autoPackCommand->placedCount(), 2);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_EQ(fixture.controller->remainingPoolSnapshot().size(), 2);

  command->execute();

  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 2);
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
}


TEST(PackingController, AutoPackAllEmptyPoolReturnsWarning)
{
  ControllerFixture fixture;

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::AutoPackAll));
  const std::optional<std::string> message = fixture.controller->takeLastMessage();

  EXPECT_EQ(command, nullptr);
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value(), "Пул фигур пуст");
}


TEST(PackingController, AutoPackAllFailureDoesNotMutateBoardOrPool)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> impossible = makeFigure({{0, 0}, {20, 0}});
  fixture.controller->loadPool({impossible});
  const std::vector<std::shared_ptr<Figure>> before = fixture.controller->remainingPoolSnapshot();

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::AutoPackAll));
  const std::optional<std::string> message = fixture.controller->takeLastMessage();

  EXPECT_EQ(command, nullptr);
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot(), before);
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value(), "Не найдено место для автоматического размещения фигуры");
}


//------------------------------------------------------------------------------
/**
  Проверяет автоматический режим через стратегию размещения preview
*/
//--
TEST(PackingController, AutomaticModeUsesStrategyForValidPreview)
{
  ControllerFixture fixture;
  const auto result = ObjectPoolLoader::loadFromText("OBJECT\nCELL 0 0\nEND_OBJECT\n");
  ASSERT_TRUE(result.success);
  ASSERT_EQ(result.figures.size(), 1);

  fixture.controller->setMode(PackingMode::Automatic);
  fixture.controller->loadPool(result.figures);

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(command.get()), nullptr);
  command->execute();
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_TRUE(fixture.controller->remainingPoolSnapshot().empty());
}


//------------------------------------------------------------------------------
/**
  Проверяет отказ автоматического режима при невалидной preview-фигуре
*/
//--
TEST(PackingController, AutomaticModeReturnsNullptrForInvalidPreview)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}, {20, 0}});

  fixture.controller->setMode(PackingMode::Automatic);
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());

  EXPECT_EQ(command, nullptr);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);
  EXPECT_EQ(fixture.mainBoard->getFigures().size(), 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет одноразовую диагностику отказа автоматического размещения
*/
//--
TEST(PackingController, AutomaticInvalidPlacementReturnsFailureMessageOnce)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}, {20, 0}});

  fixture.controller->setMode(PackingMode::Automatic);
  fixture.controller->loadPool({figure});

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());
  const std::optional<std::string> firstMessage = fixture.controller->takeLastMessage();
  const std::optional<std::string> secondMessage = fixture.controller->takeLastMessage();

  EXPECT_EQ(command, nullptr);
  ASSERT_TRUE(firstMessage.has_value());
  EXPECT_EQ(firstMessage.value(), "Не найдено место для автоматического размещения фигуры");
  EXPECT_FALSE(secondMessage.has_value());
}


//------------------------------------------------------------------------------
/**
  Проверяет диагностику выхода активной фигуры за границы без блокировки ручного перемещения
*/
//--
TEST(PackingController, ManualMoveOutsideBoardCreatesWarningMessage)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> nextFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({figure, nextFigure});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  std::shared_ptr<Figure> activeFigure = fixture.mainBoard->getFigures().front();

  std::unique_ptr<ICommand> moveCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(moveCommand, nullptr);
  moveCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  const std::optional<std::string> message = fixture.controller->takeLastMessage();

  EXPECT_EQ(activeFigure->GetCells().front().GetCoordinates().column, -1);
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value(), "Фигура выходит за границы доски");
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие повторного сообщения при неизменной причине невалидности
*/
//--
TEST(PackingController, ManualInvalidStatusDoesNotRepeatSameMessage)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> nextFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({figure, nextFigure});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  std::unique_ptr<ICommand> moveCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(moveCommand, nullptr);
  moveCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());

  fixture.controller->updateActivePlacementStatus();

  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());
}


//------------------------------------------------------------------------------
/**
  Проверяет очистку статуса после возврата активной фигуры в валидную позицию
*/
//--
TEST(PackingController, ManualValidPositionClearsInvalidStatus)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> nextFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({figure, nextFigure});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();
  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  std::shared_ptr<Figure> activeFigure = fixture.mainBoard->getFigures().front();

  std::unique_ptr<ICommand> leftCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(leftCommand, nullptr);
  leftCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());

  std::unique_ptr<ICommand> rightCommand = fixture.controller->onEvent(ActionEvent(Action::Right));
  ASSERT_NE(rightCommand, nullptr);
  rightCommand->execute();
  fixture.controller->updateActivePlacementStatus();

  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());
  EXPECT_EQ(activeFigure->GetCells().front().GetCoordinates().column, 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет диагностику пересечения активной фигуры с уже размещённой фигурой
*/
//--
TEST(PackingController, ManualIntersectionCreatesWarningMessage)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> placedFigure = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> activeFigure = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  std::shared_ptr<Figure> nextFigure = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  ASSERT_TRUE(fixture.mainBoard->add(placedFigure));
  fixture.controller->loadPool({activeFigure, nextFigure});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  std::unique_ptr<ICommand> moveCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(moveCommand, nullptr);
  moveCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  const std::optional<std::string> message = fixture.controller->takeLastMessage();

  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value(), "Фигура пересекается с уже размещённой фигурой");
}


//------------------------------------------------------------------------------
/**
  Проверяет блокировку получения следующей preview-фигуры при невалидной активной фигуре
*/
//--
TEST(PackingController, InvalidActiveFigureBlocksNextAcquire)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> second = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({first, second});

  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();
  std::unique_ptr<ICommand> moveCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(moveCommand, nullptr);
  moveCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());

  const std::vector<std::shared_ptr<Figure>> remainingBefore = fixture.controller->remainingPoolSnapshot();
  ASSERT_EQ(remainingBefore.size(), 1);
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> previewBefore = fixture.genBoard->getFigures().front();

  std::unique_ptr<ICommand> blockedCommand = fixture.controller->onEvent(mousePress());
  const std::optional<std::string> message = fixture.controller->takeLastMessage();

  EXPECT_EQ(blockedCommand, nullptr);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().size(), 1);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().front(), remainingBefore.front());
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().front(), previewBefore);
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message.value(), "Фигура выходит за границы доски");
}


//------------------------------------------------------------------------------
/**
  Проверяет разрешение получения следующей preview-фигуры при валидной активной фигуре
*/
//--
TEST(PackingController, ReloadPoolClearsStaleInvalidActiveFigure)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> second = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({first, second});

  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  std::unique_ptr<ICommand> moveCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(moveCommand, nullptr);
  moveCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());
  EXPECT_EQ(fixture.controller->onEvent(mousePress()), nullptr);
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());

  fixture.controller->loadPool({makeFigure({{0, 0}})});

  std::unique_ptr<ICommand> commandAfterReload = fixture.controller->onEvent(mousePress());
  EXPECT_FALSE(fixture.selection->findFigure(fixture.mainBoard->getFigures().front()));
  ASSERT_NE(commandAfterReload, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(commandAfterReload.get()), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет разрешение получения следующей preview-фигуры при валидной активной фигуре
*/
//--
TEST(PackingController, ValidActiveFigureAllowsNextAcquire)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> second = makeFigureAt(Point2D{0, 0}, {{1, 0}});
  fixture.controller->loadPool({first, second});
  std::unique_ptr<ICommand> firstAcquire = fixture.controller->onEvent(mousePress());
  ASSERT_NE(firstAcquire, nullptr);
  firstAcquire->execute();
  fixture.controller->updateActivePlacementStatus();
  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());

  std::unique_ptr<ICommand> secondAcquire = fixture.controller->onEvent(mousePress());

  ASSERT_NE(secondAcquire, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(secondAcquire.get()), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет, что пустой пул по-прежнему не создаёт сообщение
*/
//--
TEST(PackingController, EmptyPoolAcquireStaysSilent)
{
  ControllerFixture fixture;

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(mousePress());

  EXPECT_EQ(command, nullptr);
  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());
}


//------------------------------------------------------------------------------
/**
  Проверяет получение следующей preview-фигуры после возврата активной фигуры в валидную позицию
*/
//--
TEST(PackingController, FixedActiveFigureAllowsNextAcquire)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  std::shared_ptr<Figure> second = makeFigureAt(Point2D{0, 0}, {{0, 0}});
  fixture.controller->loadPool({first, second});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  std::unique_ptr<ICommand> leftCommand = fixture.controller->onEvent(ActionEvent(Action::Left));
  ASSERT_NE(leftCommand, nullptr);
  leftCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());
  ASSERT_EQ(fixture.controller->onEvent(mousePress()), nullptr);
  ASSERT_TRUE(fixture.controller->takeLastMessage().has_value());

  std::unique_ptr<ICommand> rightCommand = fixture.controller->onEvent(ActionEvent(Action::Right));
  ASSERT_NE(rightCommand, nullptr);
  rightCommand->execute();
  fixture.controller->updateActivePlacementStatus();
  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());

  std::unique_ptr<ICommand> nextAcquire = fixture.controller->onEvent(mousePress());

  ASSERT_NE(nextAcquire, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(nextAcquire.get()), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие команды без активной фигуры
*/
//--
//------------------------------------------------------------------------------
/**
  Проверяет удаление активной фигуры и очистку выделения
*/
//--
TEST(DeleteFigureCommand, ExecuteRemovesActiveFigureAndClearsSelection)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  FigurePool figurePool;
  figurePool.load({makeFigure({{4, 4}})});
  std::shared_ptr<Figure> activeFigure = makeFigureAt(Point2D{0, 0}, {{2, 3}});
  ASSERT_TRUE(fixture.mainBoard->add(activeFigure));
  fixture.selection->add(activeFigure);

  DeleteFigureCommand command(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  command.execute();

  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  EXPECT_EQ(activeFigure, nullptr);
  EXPECT_TRUE(fixture.selection->isEmptyFigures());
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(figurePool.previewFigure(), fixture.genBoard->getFigures().front());
}


//------------------------------------------------------------------------------
/**
  Проверяет возврат удаленной фигуры в preview с нормализованными координатами
*/
//--
TEST(DeleteFigureCommand, ExecuteReturnsNormalizedFigureAsPreview)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  FigurePool figurePool;
  figurePool.load({makeFigure({{7, 7}})});
  std::shared_ptr<Figure> activeFigure = makeFigureAt(Point2D{0, 0}, {{3, 4}, {5, 4}});
  ASSERT_TRUE(fixture.mainBoard->add(activeFigure));
  fixture.selection->add(activeFigure);

  DeleteFigureCommand command(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  command.execute();

  std::shared_ptr<Figure> previewFigure = figurePool.previewFigure();
  ASSERT_NE(previewFigure, nullptr);
  EXPECT_NE(previewFigure, activeFigure);
  EXPECT_EQ(previewFigure->topLeft().x, 5);
  EXPECT_EQ(previewFigure->topLeft().y, -5);
  ASSERT_EQ(previewFigure->GetCells().size(), 2);
  EXPECT_EQ(previewFigure->GetCells()[0].GetCoordinates().column, 0);
  EXPECT_EQ(previewFigure->GetCells()[0].GetCoordinates().row, 0);
  EXPECT_EQ(previewFigure->GetCells()[1].GetCoordinates().column, 2);
  EXPECT_EQ(previewFigure->GetCells()[1].GetCoordinates().row, 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет undo после удаления фигуры
*/
//--
TEST(DeleteFigureCommand, UndoRestoresBoardActiveSelectionAndPool)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  CommandManager commandManager;
  FigurePool figurePool;
  std::shared_ptr<Figure> oldPreview = makeFigure({{7, 7}});
  figurePool.load({oldPreview});
  presenter.showPreview(figurePool.previewFigure());
  std::shared_ptr<Figure> activeFigure = makeFigureAt(Point2D{0, 0}, {{2, 3}});
  ASSERT_TRUE(fixture.mainBoard->add(activeFigure));
  fixture.selection->add(activeFigure);

  auto command =
    std::make_unique<DeleteFigureCommand>(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  commandManager.execute(std::move(command));
  ASSERT_TRUE(fixture.mainBoard->getFigures().empty());

  commandManager.undo();

  ASSERT_EQ(fixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.mainBoard->getFigures().front(), activeFigure);
  EXPECT_TRUE(fixture.selection->findFigure(activeFigure));
  EXPECT_EQ(figurePool.previewFigure(), oldPreview);
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().front(), oldPreview);
}


//------------------------------------------------------------------------------
/**
  Проверяет redo после удаления фигуры
*/
//--
TEST(DeleteFigureCommand, RedoDeletesAgainAndRestoresPreviewState)
{
  ControllerFixture fixture;
  PreviewPoolPresenter presenter(fixture.genBoard);
  CommandManager commandManager;
  FigurePool figurePool;
  figurePool.load({makeFigure({{7, 7}})});
  std::shared_ptr<Figure> activeFigure = makeFigureAt(Point2D{0, 0}, {{2, 3}});
  ASSERT_TRUE(fixture.mainBoard->add(activeFigure));
  fixture.selection->add(activeFigure);

  auto command =
    std::make_unique<DeleteFigureCommand>(fixture.mainBoard, figurePool, presenter, fixture.selection, &activeFigure);

  commandManager.execute(std::move(command));
  const std::shared_ptr<Figure> returnedPreview = figurePool.previewFigure();
  commandManager.undo();
  commandManager.redo();

  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  EXPECT_EQ(activeFigure, nullptr);
  EXPECT_TRUE(fixture.selection->isEmptyFigures());
  EXPECT_EQ(figurePool.previewFigure(), returnedPreview);
  ASSERT_EQ(fixture.genBoard->getFigures().size(), 1);
  EXPECT_EQ(fixture.genBoard->getFigures().front(), returnedPreview);
}


//------------------------------------------------------------------------------
/**
  Проверяет отсутствие команды удаления без активной фигуры
*/
//--
TEST(PackingController, DeleteWithoutActiveFigureReturnsNullptr)
{
  ControllerFixture fixture;

  EXPECT_EQ(fixture.controller->onEvent(ActionEvent(Action::Delete)), nullptr);
  EXPECT_FALSE(fixture.controller->takeLastMessage().has_value());
}


//------------------------------------------------------------------------------
/**
  Проверяет создание команды удаления для активной фигуры
*/
//--
TEST(PackingController, DeleteActionReturnsDeleteFigureCommandForActiveFigure)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> first = makeFigure({{0, 0}});
  std::shared_ptr<Figure> second = makeFigure({{1, 0}});
  fixture.controller->loadPool({first, second});
  std::unique_ptr<ICommand> acquireCommand = fixture.controller->onEvent(mousePress());
  ASSERT_NE(acquireCommand, nullptr);
  acquireCommand->execute();

  std::unique_ptr<ICommand> deleteCommand = fixture.controller->onEvent(ActionEvent(Action::Delete));

  ASSERT_NE(deleteCommand, nullptr);
  EXPECT_NE(dynamic_cast<DeleteFigureCommand *>(deleteCommand.get()), nullptr);
  deleteCommand->execute();
  EXPECT_TRUE(fixture.mainBoard->getFigures().empty());
  ASSERT_FALSE(fixture.controller->remainingPoolSnapshot().empty());
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().front()->topLeft().x, 5);
  EXPECT_EQ(fixture.controller->remainingPoolSnapshot().front()->topLeft().y, -5);
}


TEST(PackingController, ActionWithoutActiveFigureReturnsNullptr)
{
  ControllerFixture fixture;

  EXPECT_EQ(fixture.controller->onEvent(ActionEvent(Action::Left)), nullptr);
}


//------------------------------------------------------------------------------
/**
  Проверяет сопоставление действия Up команде перемещения
*/
//--
TEST(PackingController, UpActionReturnsMoveCommandAndMovesFigureUp)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = acquireActiveFigure(fixture);
  const Coordinates before = figure->GetCells().front().GetCoordinates();

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::Up));

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<MoveCommand *>(command.get()), nullptr);

  command->execute();

  EXPECT_EQ(figure->GetCells().front().GetCoordinates().column, before.column);
  EXPECT_EQ(figure->GetCells().front().GetCoordinates().row, before.row - 1);
}


//------------------------------------------------------------------------------
/**
  Проверяет сопоставление действия Rotate команде поворота
*/
//--
TEST(PackingController, RotateActionReturnsRotateCommandAndRotatesFigure)
{
  ControllerFixture fixture;
  std::shared_ptr<Figure> figure = acquireActiveFigure(fixture);
  const std::vector<Coordinates> before = {figure->GetCells()[0].GetCoordinates(), figure->GetCells()[1].GetCoordinates(),
                                           figure->GetCells()[2].GetCoordinates(), figure->GetCells()[3].GetCoordinates()};

  std::unique_ptr<ICommand> command = fixture.controller->onEvent(ActionEvent(Action::Rotate));

  ASSERT_NE(command, nullptr);
  EXPECT_NE(dynamic_cast<RotateCommand *>(command.get()), nullptr);

  command->execute();

  const std::vector<Coordinates> after = {figure->GetCells()[0].GetCoordinates(), figure->GetCells()[1].GetCoordinates(),
                                          figure->GetCells()[2].GetCoordinates(), figure->GetCells()[3].GetCoordinates()};

  EXPECT_NE(after, before);
}


//------------------------------------------------------------------------------
/**
  Проверяет делегирование сценария MousePress из состояния упаковки
*/
//--
TEST(PackagingState, DelegatesMousePressFlowToPackingController)
{
  StateFixture stateFixture;
  ControllerFixture controllerFixture;

  stateFixture.controller->loadPool({makeFigure({{0, 0}, {1, 0}})});
  controllerFixture.controller->loadPool({makeFigure({{0, 0}, {1, 0}})});

  std::unique_ptr<ICommand> stateCommand = stateFixture.state->onEvent(mousePress());
  std::unique_ptr<ICommand> controllerCommand = controllerFixture.controller->onEvent(mousePress());

  ASSERT_NE(stateCommand, nullptr);
  ASSERT_NE(controllerCommand, nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(stateCommand.get()), nullptr);
  EXPECT_NE(dynamic_cast<PlacePreviewFigureCommand *>(controllerCommand.get()), nullptr);

  stateCommand->execute();
  controllerCommand->execute();

  ASSERT_EQ(stateFixture.mainBoard->getFigures().size(), 1);
  ASSERT_EQ(controllerFixture.mainBoard->getFigures().size(), 1);
  EXPECT_EQ(stateFixture.mainBoard->getFigures().front()->topLeft().x, 0);
  EXPECT_EQ(stateFixture.mainBoard->getFigures().front()->topLeft().y, 0);
  EXPECT_EQ(controllerFixture.mainBoard->getFigures().front()->topLeft().x, 0);
  EXPECT_EQ(controllerFixture.mainBoard->getFigures().front()->topLeft().y, 0);
}


//------------------------------------------------------------------------------
/**
  Проверяет делегирование сценария Action из состояния упаковки
*/
//--
TEST(PackagingState, DelegatesActionFlowToPackingController)
{
  StateFixture stateFixture;
  ControllerFixture controllerFixture;

  std::shared_ptr<Figure> stateFigure = makeFigure({{0, 0}, {1, 0}});
  std::shared_ptr<Figure> stateNextFigure = makeFigure({{2, 0}});
  std::shared_ptr<Figure> controllerFigure = makeFigure({{0, 0}, {1, 0}});
  std::shared_ptr<Figure> controllerNextFigure = makeFigure({{2, 0}});
  stateFixture.controller->loadPool({stateFigure, stateNextFigure});
  controllerFixture.controller->loadPool({controllerFigure, controllerNextFigure});

  std::unique_ptr<ICommand> stateAcquire = stateFixture.state->onEvent(mousePress());
  std::unique_ptr<ICommand> controllerAcquire = controllerFixture.controller->onEvent(mousePress());
  ASSERT_NE(stateAcquire, nullptr);
  ASSERT_NE(controllerAcquire, nullptr);
  stateAcquire->execute();
  controllerAcquire->execute();
  ASSERT_EQ(stateFixture.mainBoard->getFigures().size(), 1);
  ASSERT_EQ(controllerFixture.mainBoard->getFigures().size(), 1);
  const std::shared_ptr<Figure> statePlacedFigure = stateFixture.mainBoard->getFigures().front();
  const std::shared_ptr<Figure> controllerPlacedFigure = controllerFixture.mainBoard->getFigures().front();
  const Coordinates stateBefore = statePlacedFigure->GetCells().front().GetCoordinates();
  const Coordinates controllerBefore = controllerPlacedFigure->GetCells().front().GetCoordinates();

  std::unique_ptr<ICommand> stateCommand = stateFixture.state->onEvent(ActionEvent(Action::Left));
  std::unique_ptr<ICommand> controllerCommand = controllerFixture.controller->onEvent(ActionEvent(Action::Left));

  ASSERT_NE(stateCommand, nullptr);
  ASSERT_NE(controllerCommand, nullptr);
  EXPECT_NE(dynamic_cast<MoveCommand *>(stateCommand.get()), nullptr);
  EXPECT_NE(dynamic_cast<MoveCommand *>(controllerCommand.get()), nullptr);

  stateCommand->execute();
  controllerCommand->execute();

  EXPECT_EQ(statePlacedFigure->GetCells().front().GetCoordinates().column, stateBefore.column - 1);
  EXPECT_EQ(statePlacedFigure->GetCells().front().GetCoordinates().row, stateBefore.row);
  EXPECT_EQ(controllerPlacedFigure->GetCells().front().GetCoordinates().column, controllerBefore.column - 1);
  EXPECT_EQ(controllerPlacedFigure->GetCells().front().GetCoordinates().row, controllerBefore.row);
}
