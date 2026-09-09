#include <algorithm>
#include <limits>
#include <utility>

#include <actionevent.h>
#include <algs.h>
#include <autopackcommand.h>
#include <board.h>
#include <deletefigurecommand.h>
#include <event.h>
#include <firstfitautomaticplacementstrategy.h>
#include <iview.h>
#include <objectfigure.h>
#include <packagingstatisticsservice.h>
#include <packingcontroller.h>
#include <placepreviewfigurecommand.h>
#include <scenemouseevent.h>
#include <selectionmodel.h>

namespace
{
bool isInteger(double value)
{
  return std::isfinite(value) && std::floor(value) == value;
}

bool canCastToInt(double value)
{
  return isInteger(value) && value >= static_cast<double>(std::numeric_limits<int>::min()) &&
         value <= static_cast<double>(std::numeric_limits<int>::max());
}

bool canAddInt(int lhs, int rhs)
{
  if (rhs > 0 && lhs > std::numeric_limits<int>::max() - rhs)
    return false;
  if (rhs < 0 && lhs < std::numeric_limits<int>::min() - rhs)
    return false;
  return true;
}

PackingSceneObject makeSceneObject(const std::shared_ptr<Figure> & figure, size_t index)
{
  PackingSceneObject object;
  object.id = "object_" + std::to_string(index);

  if (!figure || figure->GetCells().empty())
    return object;

  int minColumn = std::numeric_limits<int>::max();
  int minRow = std::numeric_limits<int>::max();

  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    minColumn = std::min(minColumn, coords.column);
    minRow = std::min(minRow, coords.row);
  }

  object.cells.reserve(figure->GetCells().size());
  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    object.cells.push_back({coords.column - minColumn, coords.row - minRow});
  }

  return object;
}

PackingSceneAction makeSceneAction(const PackingSceneObject & object, const std::shared_ptr<Figure> & figure)
{
  PackingSceneAction action;
  action.objectId = object.id;

  if (!figure || figure->GetCells().empty())
    return action;

  int minColumn = std::numeric_limits<int>::max();
  int minRow = std::numeric_limits<int>::max();

  for (const Cell2D & cell : figure->GetCells())
  {
    const Coordinates coords = cell.GetCoordinates();
    minColumn = std::min(minColumn, coords.column);
    minRow = std::min(minRow, coords.row);
  }

  action.x = static_cast<double>(minColumn);
  action.y = static_cast<double>(minRow);
  action.rotationDegrees = 0.0;
  return action;
}

const PackingSceneObject * findSceneObject(const PackingSceneSnapshot & snapshot, const std::string & objectId)
{
  auto it =
    std::ranges::find_if(snapshot.objects, [&objectId](const PackingSceneObject & object) { return object.id == objectId; });

  if (it == snapshot.objects.end())
    return nullptr;

  return &(*it);
}

std::shared_ptr<Figure> makeFigureFromSceneAction(const PackingSceneObject & object, const PackingSceneAction & action)
{
  std::vector<Cell2D> cells;
  cells.reserve(object.cells.size());

  const int offsetColumn = static_cast<int>(action.x);
  const int offsetRow = static_cast<int>(action.y);
  for (const PackingSceneCell & cell : object.cells)
    cells.push_back(CreateSquareCell({cell.column + offsetColumn, cell.row + offsetRow}));

  return std::make_shared<ObjectFigure>(Point2D{0, 0}, std::move(cells));
}

bool canCreateFigureFromSceneAction(const PackingSceneObject & object, const PackingSceneAction & action)
{
  if (!canCastToInt(action.x) || !canCastToInt(action.y))
    return false;

  const int offsetColumn = static_cast<int>(action.x);
  const int offsetRow = static_cast<int>(action.y);
  for (const PackingSceneCell & cell : object.cells)
  {
    if (!canAddInt(cell.column, offsetColumn) || !canAddInt(cell.row, offsetRow))
      return false;
  }

  return true;
}
} // namespace

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
PackingController::PackingController(std::shared_ptr<IStatisticsView> view, std::shared_ptr<SelectionModel> selection,
                                     std::shared_ptr<Board> main, std::shared_ptr<Board> gen)
  : selection_(std::move(selection))
  , mainBoard_(std::move(main))
  , genBoard_(std::move(gen))
  , boardAnalyzer_(BoardAnalyzer::create(mainBoard_))
  , view_(std::move(view))
  , previewPresenter_(genBoard_)
  , automaticPlacementStrategy_(std::make_unique<FirstFitAutomaticPlacementStrategy>())
  , mode_(PackingMode::Manual)
{
}


//------------------------------------------------------------------------------
/**
  Обрабатывает события режима упаковки
*/
//--
std::unique_ptr<ICommand> PackingController::onEvent(const Event & event)
{
  switch (event.type())
  {
    case EventType::MousePress:
    {
      const SceneMouseEvent & mousePressEvent = static_cast<const SceneMouseEvent &>(event);
      return onMouseEvent(mousePressEvent);
    }
    case EventType::Action:
    {
      const ActionEvent & actionRequestEvent = static_cast<const ActionEvent &>(event);
      return onActionEvent(actionRequestEvent);
    }
    default:
      return nullptr;
  }
}


//------------------------------------------------------------------------------
/**
  Загружает пул фигур
*/
//--
void PackingController::loadPool(std::vector<std::shared_ptr<Figure>> figures)
{
  figurePool_.load(std::move(figures));
  activeFig_.reset();
  selection_->clearFigures();
  activePlacementStatus_.reset();
  lastMessage_.reset();
  syncPreviewFigure();
}


//------------------------------------------------------------------------------
/**
  Возвращает snapshot оставшегося пула
*/
//--
std::vector<std::shared_ptr<Figure>> PackingController::remainingPoolSnapshot() const
{
  return figurePool_.snapshotRemaining();
}


//------------------------------------------------------------------------------
/**
  Снимает snapshot текущей сцены упаковки
*/
//--
PackingSceneSnapshot PackingController::packingSceneSnapshot() const
{
  PackingSceneSnapshot snapshot;
  snapshot.board.columns = mainBoard_->countColumns();
  snapshot.board.rows = mainBoard_->countRows();

  const std::vector<std::shared_ptr<Figure>> figures = mainBoard_->getFigures();
  snapshot.objects.reserve(figures.size());
  snapshot.actions.reserve(figures.size());

  for (size_t index = 0; index < figures.size(); ++index)
  {
    PackingSceneObject object = makeSceneObject(figures[index], index);
    PackingSceneAction action = makeSceneAction(object, figures[index]);

    snapshot.objects.push_back(std::move(object));
    snapshot.actions.push_back(std::move(action));
  }

  return snapshot;
}


//------------------------------------------------------------------------------
/**
  Загружает snapshot сцены упаковки
*/
//--
PackingSceneLoadResult PackingController::loadSceneSnapshot(const PackingSceneSnapshot & snapshot)
{
  if (snapshot.board.columns != mainBoard_->countColumns() || snapshot.board.rows != mainBoard_->countRows())
    return {false, "Размер доски сцены не совпадает с текущей доской"};

  std::shared_ptr<Board> validationBoard = Board::create(snapshot.board.columns, snapshot.board.rows);
  std::vector<std::shared_ptr<Figure>> loadedFigures;
  loadedFigures.reserve(snapshot.actions.size());

  // Сцена сначала собирается на временной доске: ошибка не должна менять текущую сессию.
  auto clearValidationBoard = [&validationBoard, &loadedFigures]()
  {
    for (const std::shared_ptr<Figure> & figure : loadedFigures)
      validationBoard->remove(figure);
  };

  for (const PackingSceneAction & action : snapshot.actions)
  {
    if (action.type != "place" || action.rotationDegrees != 0.0)
    {
      clearValidationBoard();
      return {false, "Сцена содержит неподдерживаемое действие размещения"};
    }

    const PackingSceneObject * object = findSceneObject(snapshot, action.objectId);
    if (!object)
    {
      clearValidationBoard();
      return {false, "Сцена содержит действие для неизвестного объекта"};
    }

    if (!canCreateFigureFromSceneAction(*object, action))
    {
      clearValidationBoard();
      return {false, "Сцена содержит некорректные координаты размещения"};
    }

    std::shared_ptr<Figure> figure = makeFigureFromSceneAction(*object, action);
    const PlacementValidationResult validation = placementValidator_.validate(validationBoard, figure);
    if (!validation.canPlace())
    {
      clearValidationBoard();
      return {false, validation.message()};
    }

    validationBoard->add(figure);
    loadedFigures.push_back(std::move(figure));
  }

  clearValidationBoard();

  for (const std::shared_ptr<Figure> & figure : mainBoard_->getFigures())
    mainBoard_->remove(figure);

  for (const std::shared_ptr<Figure> & figure : loadedFigures)
    mainBoard_->add(figure);

  figurePool_.clear();
  activeFig_.reset();
  selection_->clearFigures();
  activePlacementStatus_.reset();
  lastMessage_.reset();
  syncPreviewFigure();

  return {true, {}};
}


//------------------------------------------------------------------------------
/**
  Проверяет возможность сохранения текущей сцены упаковки
*/
//--
bool PackingController::canSavePackingScene() const
{
  for (const std::shared_ptr<Figure> & figure : mainBoard_->getFigures())
  {
    if (!placementValidator_.validate(mainBoard_, figure).canPlace())
      return false;
  }

  return true;
}


//------------------------------------------------------------------------------
/**
  Возвращает текущий режим упаковки
*/
//--
PackingMode PackingController::mode() const
{
  return mode_;
}


//------------------------------------------------------------------------------
/**
  Устанавливает режим упаковки
*/
//--
void PackingController::setMode(PackingMode mode)
{
  mode_ = mode;
  activePlacementStatus_.reset();
}


//------------------------------------------------------------------------------
/**
  Обрабатывает событие мыши на сцене упаковки
*/
//--
std::unique_ptr<ICommand> PackingController::onMouseEvent(const SceneMouseEvent & event)
{
  if (event.type() != EventType::MousePress)
    return nullptr;

  switch (mode_)
  {
    case PackingMode::Manual:
      lastMessage_.reset();
      return acquireFigureFromGenerator();
    case PackingMode::Automatic:
      return runAutomaticPlacement();
  }

  return nullptr;
}


//------------------------------------------------------------------------------
/**
  Обрабатывает событие действия пользователя
*/
//--
std::unique_ptr<ICommand> PackingController::onActionEvent(const ActionEvent & event)
{
  if (event.action() == Action::AutoPackAll)
    return runAutomaticPackRemaining();

  if (event.action() == Action::AutoPlace)
    return runAutomaticPlacement();

  if (event.action() == Action::Delete)
  {
    if (!activeFig_)
      return nullptr;

    return std::make_unique<DeleteFigureCommand>(mainBoard_, figurePool_, previewPresenter_, selection_, &activeFig_);
  }

  return actionMapper_.commandFor(event.action(), activeFig_);
}


//------------------------------------------------------------------------------
/**
  Перемещает фигуру со сцены генерации на основную сцену
*/
//--
std::unique_ptr<ICommand> PackingController::acquireFigureFromGenerator()
{
  if (!figurePool_.hasPreview())
    return nullptr;

  if (!validateActiveBeforeAcquire())
    return nullptr;

  return std::make_unique<PlacePreviewFigureCommand>(mainBoard_, figurePool_, previewPresenter_, selection_, &activeFig_);
}


//------------------------------------------------------------------------------
/**
  Проверяет активную фигуру перед получением следующей preview-фигуры
*/
//--
bool PackingController::validateActiveBeforeAcquire()
{
  if (!activeFig_)
    return true;

  const PlacementValidationResult result = placementValidator_.validate(mainBoard_, activeFig_);
  if (result.canPlace())
  {
    activePlacementStatus_.reset();
    lastMessage_.reset();
    return true;
  }

  activePlacementStatus_ = result.code;
  lastMessage_ = result.message();
  return false;
}


//------------------------------------------------------------------------------
/**
  Выполняет сценарий автоматического размещения preview-фигуры
*/
//--
std::unique_ptr<ICommand> PackingController::runAutomaticPlacement()
{
  AutomaticPlacementResult result =
    automaticPlacementStrategy_->createPlacement(mainBoard_, figurePool_, previewPresenter_, selection_, &activeFig_);

  if (result.command)
  {
    lastMessage_.reset();
    return std::move(result.command);
  }

  if (result.hasFailure())
    lastMessage_ = result.validation.message();
  else
    lastMessage_.reset();

  return nullptr;
}


//------------------------------------------------------------------------------
/**
  РЎС‚СЂРѕРёС‚ all-or-nothing РєРѕРјР°РЅРґСѓ Р°РІС‚РѕСѓРїР°РєРѕРІРєРё РѕСЃС‚Р°РІС€РµРіРѕСЃСЏ РїСѓР»Р°
*/
//--
std::unique_ptr<ICommand> PackingController::runAutomaticPackRemaining()
{
  const AutomaticPackPlan plan = automaticPackPlanner_.createPlan(mainBoard_, figurePool_, *automaticPlacementStrategy_);

  if (!plan.success())
  {
    lastMessage_ = plan.validation.message();
    return nullptr;
  }

  // Планировщик уже нашёл все позиции; команда только применяет готовую серию через обычный путь команд.
  std::vector<std::unique_ptr<PlacePreviewFigureCommand>> commands;
  commands.reserve(plan.placements.size());
  for (const std::shared_ptr<Figure> & placement : plan.placements)
  {
    commands.push_back(std::make_unique<PlacePreviewFigureCommand>(
      mainBoard_, figurePool_, previewPresenter_, selection_, &activeFig_, placement, PreviewConsumptionPolicy::ConsumePreview));
  }

  lastMessage_.reset();
  return std::make_unique<AutoPackCommand>(std::move(commands));
}


//------------------------------------------------------------------------------
/**
  Синхронизирует preview фигуру со сценой генерации
*/
//--
void PackingController::syncPreviewFigure()
{
  previewPresenter_.showPreview(figurePool_.previewFigure());
}


//------------------------------------------------------------------------------
/**
  Обновляет статистику упаковки
*/
//--
void PackingController::updateStatistic()
{
  PackagingStatisticsService::update(boardAnalyzer_, view_);
}


//------------------------------------------------------------------------------
/**
  Обновляет статус валидности активной фигуры
*/
//--
void PackingController::updateActivePlacementStatus()
{
  if (!activeFig_)
  {
    activePlacementStatus_.reset();
    lastMessage_.reset();
    return;
  }

  const PlacementValidationResult result = placementValidator_.validate(mainBoard_, activeFig_);
  if (result.canPlace())
  {
    activePlacementStatus_.reset();
    lastMessage_.reset();
    return;
  }

  if (!activePlacementStatus_.has_value() || activePlacementStatus_.value() != result.code)
    lastMessage_ = result.message();

  activePlacementStatus_ = result.code;
}


//------------------------------------------------------------------------------
/**
  Забирает последнее сообщение для пользователя
*/
//--
std::optional<std::string> PackingController::takeLastMessage()
{
  std::optional<std::string> message = std::move(lastMessage_);
  lastMessage_.reset();
  return message;
}
