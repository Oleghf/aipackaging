#ifndef AIPACKAGING_APPLICATION_POLYGONWORKSPACEPRESENTATION_H
#define AIPACKAGING_APPLICATION_POLYGONWORKSPACEPRESENTATION_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// Содержит компоненты целевой функции, необходимые интерфейсу.
struct NestingObjectiveSummary
{
  std::uint64_t usedLength = 0;
  std::uint64_t primaryRemnantWidth = 0;
  std::uint64_t largestExtraRectangleArea = 0;
  std::uint64_t fragmentationPenalty = 0;
  std::size_t placedParts = 0;
  std::size_t totalParts = 0;
  double materialUtilization = 0.0;
};

/// Содержит диагностические счётчики, отображаемые пользователю.
struct NestingMetricsSummary
{
  std::uint64_t candidatesGenerated = 0;
  std::uint64_t expandedStates = 0;
  std::uint64_t totalTimeUs = 0;
};

/// Точка полигональной сцены в миллиметрах с осью Y вверх.
struct PolygonViewPoint
{
  double x = 0.0;
  double y = 0.0;
};

/// Размещённая деталь, подготовленная для отрисовки без геометрических вычислений.
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

/// Обозначает измеряемый этап фонового поиска.
enum class NestingProgressStage : std::uint8_t
{
  Instances,
  RandomIterations,
  ExpandedStates,
  NeuralRollouts
};

/// Описывает ход выполнения одного фонового запуска.
struct NestingProgress
{
  NestingProgressStage stage = NestingProgressStage::Instances;
  std::uint64_t completed = 0;
  std::uint64_t total = 0;
  std::uint64_t expandedStates = 0;
};

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
  bool canLoadModel = true;
  bool modelReady = false;
  std::string modelId;
  std::string modelSha256;
  std::string modelStatusText;
  NestingProgress progress;
  NestingObjectiveSummary objective;
  NestingMetricsSummary metrics;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
};

#endif
