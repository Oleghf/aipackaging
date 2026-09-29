#ifndef AIPACKAGING_APPLICATION_POLYGONIMPORTCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONIMPORTCONTRACTS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <polygoneditablecontracts.h>
#include <polygonidentifiers.h>

/// Обозначает выбранную пользователем единицу длины импортируемого документа.
enum class PolygonImportUnit : std::uint8_t
{
  Unknown,
  Millimeter,
  Centimeter,
  Meter,
  Kilometer,
  Inch,
  Foot,
  Yard,
  Micrometer
};

/// Описывает слой импортируемого документа без раскрытия типов DXF.
struct PolygonImportLayer
{
  std::string name;
  std::size_t supportedEntities = 0;
  std::size_t unsupportedEntities = 0;
  bool selected = true;
};

/// Описывает распознанную цепочку для предпросмотра и назначения детали.
struct PolygonImportPath
{
  std::uint64_t id = 0;
  std::string layer;
  bool closed = false;
  double minX = 0.0;
  double minY = 0.0;
  double maxX = 0.0;
  double maxY = 0.0;
  std::size_t segmentCount = 0;
  std::uint64_t suggestedOuterPath = 0;
};

/// Описывает одно предлагаемое соединение почти совпадающих концов.
struct PolygonImportJoin
{
  std::uint64_t id = 0;
  std::uint64_t firstPath = 0;
  bool firstAtStart = false;
  std::uint64_t secondPath = 0;
  bool secondAtStart = false;
  double distanceMm = 0.0;
};

/// Описывает диагностическое сообщение импорта с исходным положением.
struct PolygonImportDiagnostic
{
  bool error = false;
  std::size_t line = 0;
  std::string layer;
  std::string entityType;
  std::string message;
};

/// Хранит нейтральный результат фонового анализа выбранного файла.
struct PolygonImportInspection
{
  bool success = false;
  std::string error;
  PolygonImportSessionHandle session;
  std::string sourceIdentifier;
  PolygonImportUnit detectedUnit = PolygonImportUnit::Unknown;
  bool unitSelectionRequired = true;
  std::vector<PolygonImportLayer> layers;
  std::vector<PolygonImportPath> paths;
  std::vector<PolygonImportJoin> joinProposals;
  std::vector<PolygonImportDiagnostic> diagnostics;
};

/// Задаёт путь, единицу и допуск для повторяемого анализа исходного файла.
struct PolygonImportInspectionRequest
{
  std::string filePath;
  PolygonImportUnit unit = PolygonImportUnit::Unknown;
  double joinToleranceMm = 0.01;
  std::vector<std::uint64_t> acceptedJoinProposals;
};

/// Назначает внешнюю цепочку и отверстия одному типу детали.
struct PolygonImportPart
{
  std::uint64_t outerPath = 0;
  std::vector<std::uint64_t> holes;
  std::string partId;
  std::uint32_t quantity = 1;
  std::vector<int> allowedRotations{0};
};

/// Содержит все подтверждённые настройки построения импортированного документа.
struct PolygonImportConfiguration
{
  PolygonImportUnit unit = PolygonImportUnit::Unknown;
  std::vector<std::string> selectedLayers;
  double joinToleranceMm = 0.01;
  std::vector<std::uint64_t> acceptedJoinProposals;
  bool ignoreUnsupported = false;
  bool groupDuplicateParts = false;
  std::vector<PolygonImportPart> parts;
  std::string problemId;
  double sheetWidth = 0.0;
  double sheetHeight = 0.0;
  double sheetMargin = 0.0;
  double partSpacing = 0.0;
  double kerf = 0.0;
  double curveTolerance = 0.05;
};

/// Возвращает построенный документ и диагностику точной проверки.
struct PolygonImportBuildResult
{
  bool success = false;
  std::string error;
  PolygonEditableDocumentLoadResult document;
  std::vector<PolygonImportDiagnostic> diagnostics;
};

/// Обозначает состояние отдельного пользовательского сценария импорта.
enum class PolygonImportState : std::uint8_t
{
  Empty,
  Inspecting,
  Ready,
  Building,
  Completed,
  Cancelled,
  Error
};

/// Содержит согласованное состояние пошагового мастера импорта.
struct PolygonImportSnapshot
{
  PolygonImportState state = PolygonImportState::Empty;
  PolygonImportInspection inspection;
  std::vector<PolygonImportDiagnostic> diagnostics;
  std::vector<aipackaging::editor::DocumentDiagnostic> documentDiagnostics;
  bool resultValid = false;
  bool canBuild = false;
  bool canAcceptProblem = false;
  bool canAcceptDraft = false;
  std::string statusText;
};

/// Набор прикладных действий отдельного мастера импорта.
struct PolygonImportActions
{
  std::function<void(const PolygonImportInspectionRequest &)> inspect;
  std::function<void(const PolygonImportConfiguration &)> build;
  std::function<void()> cancel;
  std::function<void(bool)> accept;
  std::function<void()> close;
};

/// Выполняет предметные операции импорта и владеет временными сеансами.
class IPolygonImportGateway
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonImportGateway() = default;
  /// Читает файл, выполняет строгий анализ и регистрирует сеанс.
  virtual PolygonImportInspection inspect(const PolygonImportInspectionRequest & request) = 0;
  /// Строит документ из ранее проверенного сеанса и подтверждённых настроек.
  virtual PolygonImportBuildResult build(PolygonImportSessionHandle session,
                                         const PolygonImportConfiguration & configuration) = 0;
  /// Без исключений освобождает временный сеанс.
  virtual void release(PolygonImportSessionHandle session) noexcept = 0;
  /// Без исключений освобождает не переданный приложению снимок документа.
  virtual void release(const PolygonImportBuildResult & result) noexcept = 0;
};

/// Содержит функции завершения фоновой работы импорта.
struct PolygonImportJobCallbacks
{
  /// Возвращает `true`, если получатель принял владение сеансом анализа.
  std::function<bool(PolygonImportJobHandle, PolygonImportInspection)> inspected;
  /// Возвращает `true`, если получатель принял владение построенным документом.
  std::function<bool(PolygonImportJobHandle, PolygonImportBuildResult)> completed;
  std::function<void(PolygonImportJobHandle, const std::string &)> failed;
  std::function<void(PolygonImportJobHandle)> cancelled;
};

/// Выполняет чтение и построение импортированного документа вне прикладного потока.
class IPolygonImportJobRunner
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonImportJobRunner() = default;
  /// Запускает анализ файла либо возвращает причину отказа.
  virtual std::optional<PolygonImportJobHandle> inspect(const PolygonImportInspectionRequest & request,
                                                        PolygonImportJobCallbacks callbacks, std::string & error) = 0;
  /// Запускает построение документа из существующего сеанса.
  virtual std::optional<PolygonImportJobHandle> build(PolygonImportSessionHandle session,
                                                      const PolygonImportConfiguration & configuration,
                                                      PolygonImportJobCallbacks callbacks, std::string & error) = 0;
  /// Запрашивает отмену совпадающей работы.
  virtual void cancel(PolygonImportJobHandle job) noexcept = 0;
  /// Освобождает завершённый или закрытый сеанс.
  virtual void release(PolygonImportSessionHandle session) noexcept = 0;
  /// Освобождает не принятый документ и его точный снимок.
  virtual void release(const PolygonImportBuildResult & result) noexcept = 0;
};

/// Принимает согласованные снимки отдельного сценария импорта.
class IPolygonImportOutput
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonImportOutput() = default;
  /// Показывает новый снимок состояния мастера импорта.
  virtual void presentPolygonImport(const PolygonImportSnapshot & snapshot) = 0;
};

#endif
