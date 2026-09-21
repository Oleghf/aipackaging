#ifndef AIPACKAGING_APPLICATION_POLYGONWORKSPACEVIEW_H
#define AIPACKAGING_APPLICATION_POLYGONWORKSPACEVIEW_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Непостоянный идентификатор загруженной полигональной задачи в текущем процессе.
struct PolygonDocumentHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный объект.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonDocumentHandle &, const PolygonDocumentHandle &) = default;
};

/// Непостоянный идентификатор проверенного решения в текущем процессе.
struct PolygonSolutionHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный объект.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonSolutionHandle &, const PolygonSolutionHandle &) = default;
};

/// Непостоянный идентификатор проверенного комплекта модели в текущем процессе.
struct PolygonModelHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированную модель.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const PolygonModelHandle &, const PolygonModelHandle &) = default;
};

/// Непостоянный идентификатор фонового запуска в текущем процессе.
struct NestingJobHandle
{
  std::uint64_t value = 0;
  /// Сообщает, ссылается ли идентификатор на зарегистрированный запуск.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает два идентификатора одной процессной области.
  friend bool operator==(const NestingJobHandle &, const NestingJobHandle &) = default;
};

/// Выбирает семейство внутренней реализации раскроя.
enum class NestingMethod : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid
};

/// Выбирает один из пяти базовых алгоритмов.
enum class BaselineAlgorithm : std::uint8_t
{
  InputFirstFit,
  AreaLeftBottom,
  MaxSideLeftBottom,
  RandomLeftBottom,
  Beam
};

/// Выбирает жадный или многократный способ применения нейросетевой политики.
enum class NeuralSelectionMode : std::uint8_t
{
  Greedy,
  BestOf
};

/// Задаёт параметры одного запуска без раскрытия типов поискового модуля.
struct NestingRunRequest
{
  NestingMethod method = NestingMethod::Baseline;
  BaselineAlgorithm algorithm = BaselineAlgorithm::AreaLeftBottom;
  NeuralSelectionMode neuralSelection = NeuralSelectionMode::BestOf;
  std::optional<PolygonModelHandle> model;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50'000;
  std::uint64_t timeoutMs = 30'000;
  std::size_t neuralRollouts = 16;
  std::size_t fallbackRandomIterations = 64;
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

/// Обозначает причину завершения раскроя.
enum class NestingCompletion : std::uint8_t
{
  Solved,
  NoSolutionFound,
  BudgetExhausted,
  TimedOut,
  UnsupportedEnvironment,
  Cancelled
};

/// Обозначает фактически использованный источник результата.
enum class NestingProvenance : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid,
  HybridFallback
};

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

/// Описывает результат, уже подготовленный и проверенный инфраструктурой.
struct NestingRunResult
{
  NestingCompletion completion = NestingCompletion::NoSolutionFound;
  NestingProvenance provenance = NestingProvenance::Baseline;
  std::string implementationName;
  std::string diagnostic;
  std::string solutionStatus;
  bool partial = true;
  NestingObjectiveSummary objective;
  NestingMetricsSummary metrics;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
  std::optional<PolygonSolutionHandle> solution;
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

/// Результат загрузки полигонального документа через прикладной порт.
struct PolygonDocumentLoadResult
{
  bool success = false;
  std::string error;
  PolygonDocumentHandle document;
  std::string problemId;
  PolygonSceneView scene;
  std::vector<std::string> unplacedInstances;
};

/// Результат файловой операции без исключений в пользовательском сценарии.
struct PolygonDocumentOperationResult
{
  bool success = false;
  std::string error;
};

/// Результат строгой загрузки внешнего комплекта полигональной модели.
struct PolygonModelLoadResult
{
  bool success = false;
  std::string error;
  PolygonModelHandle model;
  std::string modelId;
  std::string modelSha256;
};

/// Набор функций для сообщений о ходе и результате работы.
struct NestingJobCallbacks
{
  std::function<void(NestingJobHandle, const NestingProgress &)> progress;
  std::function<void(NestingJobHandle, NestingRunResult)> completed;
  std::function<void(NestingJobHandle, const std::string &)> failed;
};

/// Набор действий полигональной вкладки, привязываемых в корне композиции.
struct PolygonWorkspaceActions
{
  std::function<void(const std::string &)> openProblem;
  std::function<void(const std::string &)> saveSolution;
  std::function<bool(const std::string &)> openModel;
  std::function<void(const NestingRunRequest &)> start;
  std::function<void()> cancel;
};

/// Загружает и освобождает внешние комплекты модели без раскрытия ONNX Runtime.
class IPolygonModelGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonModelGateway() = default;
  /// Проверяет комплект модели и возвращает процессный идентификатор.
  virtual PolygonModelLoadResult load(const std::string & directory) = 0;
  /// Освобождает больше не используемую модель текущего процесса.
  virtual void release(PolygonModelHandle model) noexcept = 0;
};

/// Загружает и сохраняет документы, скрывая JSON и файловую систему.
class IPolygonDocumentGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonDocumentGateway() = default;
  /// Загружает документ и возвращает представление только после полной проверки.
  virtual PolygonDocumentLoadResult load(const std::string & filePath) = 0;
  /// Сохраняет ранее проверенное решение в указанный файл.
  virtual PolygonDocumentOperationResult save(const std::string & filePath, PolygonSolutionHandle solution) = 0;
  /// Освобождает больше не используемую задачу текущего процесса.
  virtual void release(PolygonDocumentHandle document) noexcept = 0;
  /// Освобождает больше не используемое решение текущего процесса.
  virtual void release(PolygonSolutionHandle solution) noexcept = 0;
};

/// Выполняет раскрой вне прикладного потока и доставляет типизированные события.
class INestingJobRunner
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~INestingJobRunner() = default;
  /// Запускает одну работу и возвращает её идентификатор либо причину отказа.
  virtual std::optional<NestingJobHandle> start(PolygonDocumentHandle document, const NestingRunRequest & request,
                                                NestingJobCallbacks callbacks, std::string & error) = 0;
  /// Запрашивает отмену указанной активной работы без ожидания её завершения.
  virtual void cancel(NestingJobHandle job) noexcept = 0;
};

/// Принимает неизменяемые снимки полигонального пользовательского сценария.
class IPolygonWorkspaceOutput
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonWorkspaceOutput() = default;
  /// Показывает новый согласованный снимок состояния.
  virtual void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) = 0;
};

/// Ставит функции в очередь потока, которому принадлежит контроллер.
class IApplicationDispatcher
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IApplicationDispatcher() = default;
  /// Ставит функцию в очередь без синхронного вызова и ожидания её выполнения.
  virtual void post(std::function<void()> callback) = 0;
};

/// Управляет одним способом получения проверенного результата раскроя.
class IPolygonNestingBackend
{
public:
  /// Управление отменой и ходом одной операции внутренней реализации.
  struct Control
  {
    std::function<bool()> cancellationRequested;
    std::function<void(const NestingProgress &)> progress;
  };

  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonNestingBackend() = default;
  /// Выполняет запрос и возвращает только прикладные структуры результата.
  virtual NestingRunResult run(PolygonDocumentHandle document, const NestingRunRequest & request, const Control & control) = 0;
};

#endif
