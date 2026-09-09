#ifndef PACKINGCONTROLLER_H__
#define PACKINGCONTROLLER_H__

#include <memory>
#include <optional>
#include <string>

#include <boardanalyzer.h>
#include <icommand.h>
#include <packing/automaticpackplanner.h>
#include <packing/figurepool.h>
#include <packing/iautomaticplacementstrategy.h>
#include <packing/packingactionmapper.h>
#include <packing/packingscenesnapshot.h>
#include <packing/placementvalidator.h>
#include <packing/previewpoolpresenter.h>

class ActionEvent;
class Board;
class Event;
class Figure;
class IStatisticsView;
class SceneMouseEvent;
class SelectionModel;
enum class Action;

////////////////////////////////////////////////////////////////////////////////
//
/// Режим упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
enum class PackingMode
{
  Manual,
  Automatic
};

struct PackingSceneLoadResult
{
  bool success = false;
  std::string errorMessage;
};

////////////////////////////////////////////////////////////////////////////////
//
/// Контроллер режима упаковки
/**
*/
////////////////////////////////////////////////////////////////////////////////
class PackingController
{
public:
  // Конструктор
  PackingController(std::shared_ptr<IStatisticsView> view, std::shared_ptr<SelectionModel> selection, std::shared_ptr<Board> main,
                    std::shared_ptr<Board> gen);

  // Обработать событие упаковки и вернуть команду
  std::unique_ptr<ICommand> onEvent(const Event & event);
  // Загрузить пул фигур
  void loadPool(std::vector<std::shared_ptr<Figure>> figures);
  // Снять снимок оставшегося пула
  std::vector<std::shared_ptr<Figure>> remainingPoolSnapshot() const;
  // Снять снимок текущей сцены упаковки
  PackingSceneSnapshot packingSceneSnapshot() const;
  // Загрузить снимок сцены упаковки
  PackingSceneLoadResult loadSceneSnapshot(const PackingSceneSnapshot & snapshot);
  // Проверить возможность сохранения текущей сцены упаковки
  bool canSavePackingScene() const;

  // Текущий режим упаковки
  PackingMode mode() const;
  // Установить режим упаковки
  void setMode(PackingMode mode);
  // Обновить статистику упаковки
  void updateStatistic();
  // Обновить статус валидности активной фигуры
  void updateActivePlacementStatus();
  // Забрать последнее пользовательское сообщение
  std::optional<std::string> takeLastMessage();

private:
  // Обработать событие мыши
  std::unique_ptr<ICommand> onMouseEvent(const SceneMouseEvent & event);
  // Обработать событие действия
  std::unique_ptr<ICommand> onActionEvent(const ActionEvent & event);
  // Забрать фигуру со сцены генерации
  std::unique_ptr<ICommand> acquireFigureFromGenerator();
  // Проверить, можно ли забрать следующую фигуру предпросмотра
  bool validateActiveBeforeAcquire();
  // Выполнить сценарий автоматического размещения
  std::unique_ptr<ICommand> runAutomaticPlacement();
  std::unique_ptr<ICommand> runAutomaticPackRemaining();
  // Синхронизировать фигуру предпросмотра со сценой генерации
  void syncPreviewFigure();

private:
  std::shared_ptr<SelectionModel> selection_;
  std::shared_ptr<Board> mainBoard_;
  std::shared_ptr<Board> genBoard_;
  std::shared_ptr<BoardAnalyzer> boardAnalyzer_;
  std::shared_ptr<IStatisticsView> view_;
  std::shared_ptr<Figure> activeFig_;
  FigurePool figurePool_;
  PackingActionMapper actionMapper_;
  PreviewPoolPresenter previewPresenter_;
  PlacementValidator placementValidator_;
  AutomaticPackPlanner automaticPackPlanner_;
  std::unique_ptr<IAutomaticPlacementStrategy> automaticPlacementStrategy_;
  std::optional<std::string> lastMessage_;
  std::optional<PlacementValidationCode> activePlacementStatus_;
  PackingMode mode_;
};

#endif
