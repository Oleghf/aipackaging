#include <cmath>
#include <optional>
#include <string>

#include <applicationfileservice.h>
#include <applicationhistoryservice.h>
#include <applicationpaintservice.h>
#include <applicationstateservice.h>
#include <autopackcommand.h>
#include <board.h>
#include <commandmanager.h>
#include <drawcontroller.h>
#include <icommand.h>
#include <istate.h>
#include <iview.h>
#include <maincontroller.h>
#include <openscenefileevent.h>
#include <packingcontroller.h>
#include <polygonworkspacecontroller.h>
#include <primitiveview.h>
#include <scenepaintevent.h>
#include <scenewheelevent.h>
#include <selectionmodel.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
MainController::MainController(std::shared_ptr<IView> view)
  : messageView_(view)
  , selection_(std::make_shared<SelectionModel>())
  , commandManager_(std::make_shared<CommandManager>())
  , mainBoard_(Board::create(20, 20))
  , genBoard_(Board::create(5, 5))
{
  fileDialogView_ = std::shared_ptr<IFileDialogView>(view, static_cast<IFileDialogView *>(view.get()));
  redrawView_ = std::shared_ptr<IRedrawView>(view, static_cast<IRedrawView *>(view.get()));
  statisticsView_ = std::shared_ptr<IStatisticsView>(view, static_cast<IStatisticsView *>(view.get()));
  if (auto * polygonView = dynamic_cast<IPolygonWorkspaceView *>(view.get()))
    polygonWorkspaceView_ = std::shared_ptr<IPolygonWorkspaceView>(view, polygonView);

  drawController_ = std::make_shared<DrawController>(redrawView_, selection_, mainBoard_, genBoard_);
  packingController_ = std::make_shared<PackingController>(statisticsView_, selection_, mainBoard_, genBoard_);
  stateSession_ = ApplicationStateService::create(packingController_);
  if (polygonWorkspaceView_)
  {
    polygonWorkspaceController_ = std::make_shared<PolygonWorkspaceController>(polygonWorkspaceView_);
    polygonWorkspaceController_->bindActions();
  }

  statisticsView_->statisticChangeCountAllCells(mainBoard_->countColumns() * mainBoard_->countRows());
  statisticsView_->statisticChangeCountOccupiedCells(0);
}


//------------------------------------------------------------------------------
/**
  Обрабатывает события приложения
*/
//--
void MainController::onEvent(const Event & event)
{
  switch (event.type())
  {
    case EventType::OpenScene:
      openScene(ApplicationFileService::openLoadFileDialog(fileDialogView_, "Откройте сцену упаковки", "",
                                                           "JSON (*.json);;All Files (*)"));
      break;
    case EventType::Save:
      save(ApplicationFileService::openSaveFileDialog(fileDialogView_, "Выберите место для сохранения сцены упаковки", "",
                                                      "JSON (*.json);;All Files (*)"));
      break;
    case EventType::Load:
      load(ApplicationFileService::openLoadFileDialog(fileDialogView_, "Выберите OBJECT-файл пула фигур", "",
                                                      "Text (*.txt);;All Files (*)"));
      break;
    case EventType::Undo:
      ApplicationHistoryService::undo(commandManager_, redrawView_);
      packingController_->updateStatistic();
      packingController_->updateActivePlacementStatus();
      showPackingMessage("Размещение невалидно");
      break;
    case EventType::Redo:
      ApplicationHistoryService::redo(commandManager_, redrawView_);
      packingController_->updateStatistic();
      packingController_->updateActivePlacementStatus();
      showPackingMessage("Размещение невалидно");
      break;
    case EventType::Paint:
    {
      const ScenePaintEvent & paintEv = static_cast<const ScenePaintEvent &>(event);
      ApplicationPaintService::paint(drawController_, paintEv);
      break;
    }
    case EventType::ChangeState:
      stateSession_->toggle();
      break;
    default:
      if (std::unique_ptr<ICommand> command = stateSession_->current()->onEvent(event))
      {
        AutoPackCommand * autoPackCommand = dynamic_cast<AutoPackCommand *>(command.get());
        const size_t autoPackPlacedCount = autoPackCommand ? autoPackCommand->placedCount() : 0;

        // Все мутации проходят единый порядок: выполнение, статистика, проверка, сообщение, перерисовка.
        commandManager_->execute(std::move(command));
        packingController_->updateStatistic();
        packingController_->updateActivePlacementStatus();
        showPackingMessage("Размещение невалидно");
        if (autoPackCommand)
          messageView_->showMessage("Автоупаковка", "Размещено фигур: " + std::to_string(autoPackPlacedCount), MessageType::Info);
        redrawView_->requestRedraw();
      }
      else if (std::optional<std::string> message = packingController_->takeLastMessage())
      {
        messageView_->showMessage("Размещение невозможно", *message, MessageType::Warning);
      }
      break;
  }
}


//------------------------------------------------------------------------------
/**
  Устанавливает режим упаковки
*/
//--
void MainController::setPackingMode(PackingMode mode)
{
  packingController_->setMode(mode);
}


//------------------------------------------------------------------------------
/**
  Показывает сообщение режима упаковки, если оно есть
*/
//--
void MainController::showPackingMessage(const std::string & title)
{
  if (std::optional<std::string> message = packingController_->takeLastMessage())
    messageView_->showMessage(title, *message, MessageType::Warning);
}


//------------------------------------------------------------------------------
/**
  Открывает сохранённую JSON-сцену упаковки
*/
//--
void MainController::openScene(const std::string & filePath)
{
  if (filePath.empty())
    return;

  const auto result = ApplicationFileService::loadPackingScene(filePath);
  if (!result.success)
  {
    const std::string errorMessage = result.errorMessage.empty() ? "Не удалось открыть сцену упаковки" : result.errorMessage;
    messageView_->showMessage("Ошибка открытия сцены", errorMessage, MessageType::Error);
    return;
  }

  const PackingSceneLoadResult loadResult = packingController_->loadSceneSnapshot(result.snapshot);
  if (!loadResult.success)
  {
    const std::string errorMessage =
      loadResult.errorMessage.empty() ? "Не удалось открыть сцену упаковки" : loadResult.errorMessage;
    messageView_->showMessage("Ошибка открытия сцены", errorMessage, MessageType::Error);
    return;
  }

  commandManager_->clear();
  packingController_->updateStatistic();
  redrawView_->requestRedraw();
  messageView_->showMessage("Открытие сцены", "Сцена упаковки открыта", MessageType::Info);
}


//------------------------------------------------------------------------------
/**
  Сохраняет текущую сцену упаковки по переданному пути
*/
//--
void MainController::save(const std::string & filePath)
{
  if (filePath.empty())
    return;

  if (!packingController_->canSavePackingScene())
  {
    messageView_->showMessage("Ошибка сохранения", "Нельзя сохранить невалидную расстановку", MessageType::Error);
    return;
  }

  if (ApplicationFileService::savePackingScene(filePath, packingController_->packingSceneSnapshot()))
  {
    messageView_->showMessage("Сохранение", "Сцена упаковки сохранена", MessageType::Info);
    return;
  }

  messageView_->showMessage("Ошибка сохранения", "Не удалось сохранить сцену упаковки", MessageType::Error);
}


//------------------------------------------------------------------------------
/**
  Загружает OBJECT-пул фигур по переданному пути
*/
//--
void MainController::load(const std::string & filePath)
{
  if (filePath.empty())
    return;

  const auto result = ApplicationFileService::loadFigurePool(filePath);
  if (!result.success)
  {
    const std::string errorMessage = result.errorMessage.empty() ? "Не удалось загрузить пул фигур" : result.errorMessage;
    messageView_->showMessage("Ошибка загрузки пула", errorMessage, MessageType::Error);
    return;
  }

  packingController_->loadPool(result.figures);
  commandManager_->clear();
  redrawView_->requestRedraw();
  messageView_->showMessage("Загрузка", "Пул фигур загружен", MessageType::Info);
}
