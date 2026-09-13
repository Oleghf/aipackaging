#ifndef AIPACKAGING_APP_POLYGONWORKSPACEVIEW_H
#define AIPACKAGING_APP_POLYGONWORKSPACEVIEW_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gridtypes.h>
#include <polygonsolver.h>
#include <polygontypes.h>

/// Состояние пользовательского сценария полигонального раскроя.
enum class PolygonWorkspaceState : std::uint8_t
{
  Empty,
  Ready,
  Running,
  Completed,
  Cancelled,
  Error
};

/// Точка полигональной сцены в миллиметрах с осью Y вверх.
struct PolygonViewPoint
{
  double x = 0.0;
  double y = 0.0;
};

/// Размещённая деталь, подготовленная для отрисовки без геометрических вычислений в GUI.
struct PolygonPlacedPartView
{
  std::string partId;
  std::uint32_t instanceIndex = 0;
  std::vector<PolygonViewPoint> outer;
  std::vector<std::vector<PolygonViewPoint>> holes;
  std::size_t colorIndex = 0;
};

/// Полная модель представления листа и текущей раскладки.
struct PolygonSceneView
{
  double sheetWidth = 0.0;
  double sheetHeight = 0.0;
  double sheetMargin = 0.0;
  double usedLength = 0.0;
  double primaryRemnantWidth = 0.0;
  std::vector<PolygonPlacedPartView> placements;
};

/// Данные одного обновления полигональной вкладки.
struct PolygonWorkspaceSnapshot
{
  PolygonWorkspaceState state = PolygonWorkspaceState::Empty;
  std::string problemId;
  std::string statusText;
  std::string solverName;
  std::string solutionStatus;
  bool partial = false;
  bool canOpen = true;
  bool canRun = false;
  bool canCancel = false;
  bool canSave = false;
  aipackaging::solver::PolygonSolverProgress progress;
  aipackaging::solver::PolygonObjectiveComponents objective;
  aipackaging::solver::SolverMetrics metrics;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
};

/// Набор действий GUI, привязываемых контроллером приложения.
struct PolygonWorkspaceActions
{
  std::function<void(const std::string &)> openProblem;
  std::function<void(const std::string &)> saveSolution;
  std::function<void(const aipackaging::solver::SolverConfig &)> start;
  std::function<void()> cancel;
};

/// Абстракция полигональной вкладки, не раскрывающая Qt прикладному слою.
class IPolygonWorkspaceView
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonWorkspaceView() = default;
  /// Устанавливает обработчики пользовательских действий полигональной вкладки.
  virtual void setPolygonWorkspaceActions(PolygonWorkspaceActions actions) = 0;
  /// Показывает новый неизменяемый снимок состояния полигонального рабочего процесса.
  virtual void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) = 0;
  /// Планирует обратный вызов в потоке, которому принадлежит представление.
  virtual void postToPolygonUi(std::function<void()> callback) = 0;
};

#endif
