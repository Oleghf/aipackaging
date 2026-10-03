#ifndef AIPACKAGING_IMPORT_DXF_IMPORT_H
#define AIPACKAGING_IMPORT_DXF_IMPORT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <aipackaging/editor/polygon_document.h>
#include <aipackaging/editor/polygon_document_validation.h>

namespace aipackaging::dxf
{
/// Обозначает поддерживаемую единицу длины исходного DXF.
enum class LengthUnit : std::uint8_t
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

/// Обозначает влияние сообщения импорта на возможность построения строгой задачи.
enum class DiagnosticSeverity : std::uint8_t
{
  Information,
  Warning,
  Error
};

/// Описывает одну структурированную проблему исходного файла или преобразования.
struct Diagnostic
{
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::size_t line = 0;
  std::string layer;
  std::string entityType;
  std::string message;
  bool unsupportedEntity = false;
};

/// Содержит краткие сведения об одном слое с геометрическими сущностями.
struct LayerSummary
{
  std::string name;
  std::size_t supportedEntities = 0;
  std::size_t unsupportedEntities = 0;
};

/// Описывает распознанную цепочку до назначения её деталью или отверстием.
struct PathSummary
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

/// Описывает предлагаемое объединение двух почти совпадающих концов цепочек.
struct JoinProposal
{
  std::uint64_t id = 0;
  std::uint64_t firstPath = 0;
  bool firstAtStart = false;
  std::uint64_t secondPath = 0;
  bool secondAtStart = false;
  double distanceMm = 0.0;
};

/// Хранит результат строгого анализа DXF без изменения исходного файла.
struct Inspection
{
  bool parsed = false;
  int acadVersion = 0;
  LengthUnit detectedUnit = LengthUnit::Unknown;
  bool unitSelectionRequired = true;
  std::vector<LayerSummary> layers;
  std::vector<PathSummary> paths;
  std::vector<JoinProposal> joinProposals;
  std::vector<Diagnostic> diagnostics;
};

/// Назначает одну распознанную цепочку внешним кольцом и связывает с ней отверстия.
struct PartAssignment
{
  std::uint64_t outerPath = 0;
  std::vector<std::uint64_t> holes;
  std::string partId;
  std::uint32_t quantity = 1;
  std::vector<int> allowedRotations{0};
};

/// Задаёт подтверждённые пользователем правила построения редактируемого документа.
struct ImportOptions
{
  std::optional<LengthUnit> unitOverride;
  std::vector<std::string> selectedLayers;
  double joinToleranceMm = 0.01;
  std::vector<std::uint64_t> acceptedJoinProposals;
  bool ignoreUnsupportedOnSelectedLayers = false;
  bool groupDuplicateParts = false;
  std::vector<PartAssignment> parts;
  std::string problemId;
  double sheetWidth = 0.0;
  double sheetHeight = 0.0;
  double sheetMargin = 0.0;
  double partSpacing = 0.0;
  double kerf = 0.0;
  double curveTolerance = 0.05;
};

/// Возвращает построенный документ вместе с диагностикой импортёра и локальной модели.
struct ImportResult
{
  bool built = false;
  editor::EditablePolygonDocument document;
  std::vector<Diagnostic> diagnostics;
  std::vector<editor::DocumentDiagnostic> documentDiagnostics;
};

/// Преобразует точное имя единицы, используемое мастером, в перечисление.
std::optional<LengthUnit> parseLengthUnit(std::string_view value) noexcept;
/// Возвращает устойчивое машинное имя единицы для прикладного представления.
std::string_view lengthUnitName(LengthUnit unit) noexcept;
/// Строго анализирует содержимое ASCII DXF и формирует стабильные цепочки и предложения соединения.
Inspection inspectAsciiDxf(std::string_view contents, std::optional<LengthUnit> unitOverride = std::nullopt,
                           double joinToleranceMm = 0.01, const std::vector<std::uint64_t> & acceptedJoinProposals = {});
/// Строит редактируемый документ по подтверждённым настройкам либо возвращает диагностику отказа.
ImportResult importAsciiDxf(std::string_view contents, const ImportOptions & options);
/// Создаёт детерминированные назначения замкнутых цепочек для первоначального заполнения мастера.
std::vector<PartAssignment> proposeParts(const Inspection & inspection);
} // namespace aipackaging::dxf

#endif
