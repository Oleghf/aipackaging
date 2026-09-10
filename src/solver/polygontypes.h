#ifndef AIPACKAGING_SOLVER_POLYGONTYPES_H
#define AIPACKAGING_SOLVER_POLYGONTYPES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gridtypes.h>

namespace aipackaging::solver
{
/// Точка исходного контура в миллиметрах.
struct PolygonPointMm
{
  double x = 0.0;
  double y = 0.0;
};

/// Вид сегмента исходного аналитического контура.
enum class PolygonSegmentKind : std::uint8_t
{
  Line,
  Arc,
  CubicBezier
};

/// Сегмент пути, продолжающийся от предыдущей точки до поля end.
struct PolygonSegment
{
  PolygonSegmentKind kind = PolygonSegmentKind::Line;
  PolygonPointMm end;
  PolygonPointMm center;
  PolygonPointMm control1;
  PolygonPointMm control2;
  bool clockwise = false;
};

/// Замкнутый исходный путь с общей начальной точкой и последовательностью сегментов.
struct PolygonPath
{
  PolygonPointMm start;
  std::vector<PolygonSegment> segments;
};

/// Геометрия типа детали: внешняя граница и технологические отверстия.
struct PolygonPart
{
  std::string id;
  std::uint32_t quantity = 1;
  PolygonPath outer;
  std::vector<PolygonPath> holes;
  std::vector<int> allowedRotations;
};

/// Размер прямоугольного листа в миллиметрах.
struct PolygonSheet
{
  double width = 0.0;
  double height = 0.0;
  std::string unit = "mm";
};

/// Производственные параметры, влияющие на геометрию и последующий рез.
struct PolygonManufacturing
{
  double sheetMargin = 0.0;
  double partSpacing = 0.0;
  double kerf = 0.0;
  double curveTolerance = 0.05;
};

/// Полное исходное описание одной задачи полигонального раскроя.
struct PolygonProblem
{
  std::string problemId;
  PolygonSheet sheet;
  PolygonManufacturing manufacturing;
  std::vector<PolygonPart> parts;
  GridObjectiveDefinition objective;
};

/// Точка нормализованной геометрии в микронах.
struct PolygonPoint64
{
  std::int64_t x = 0;
  std::int64_t y = 0;

  /// Сравнивает обе координаты нормализованной точки.
  bool operator==(const PolygonPoint64 &) const = default;
};

/// Полигональное кольцо без повторения первой вершины в конце.
using PolygonRing64 = std::vector<PolygonPoint64>;

/// Нормализованная ориентация детали, используемая валидатором и поиском.
struct PolygonOrientation
{
  int rotationDegrees = 0;
  PolygonRing64 outer;
  std::vector<PolygonRing64> holes;
  std::int64_t width = 0;
  std::int64_t height = 0;
  std::uint64_t materialArea = 0;
};

/// Стабильная ссылка на обязательный экземпляр полигональной детали.
struct PolygonPartInstance
{
  std::size_t partIndex = 0;
  std::uint32_t instanceIndex = 0;
  std::uint64_t area = 0;
  std::int64_t maxDimension = 0;
};

/// Одно размещение детали в микронных координатах листа.
struct PolygonPlacement
{
  std::string partId;
  std::uint32_t instanceIndex = 0;
  std::int64_t x = 0;
  std::int64_t y = 0;
  int rotationDegrees = 0;

  /// Сравнивает все поля полигонального размещения.
  bool operator==(const PolygonPlacement &) const = default;
};

/// Действие полигональной среды совпадает с размещением экземпляра.
using PolygonAction = PolygonPlacement;

/// Компоненты качества полигональной раскладки в микронных единицах.
struct PolygonObjectiveComponents
{
  std::int64_t usedLength = 0;
  std::int64_t primaryRemnantWidth = 0;
  std::uint64_t largestExtraRectangleArea = 0;
  std::uint64_t fragmentationPenalty = 0;
  std::size_t placedParts = 0;
  std::size_t totalParts = 0;
  std::uint64_t placedArea = 0;
  std::uint64_t totalPartArea = 0;
  double materialUtilization = 0.0;
  int rasterColumns = 128;
  int rasterRows = 128;
};

/// Value-state полигональной среды с уже применёнными действиями.
struct PolygonState
{
  std::vector<unsigned char> placedInstances;
  std::vector<PolygonPlacement> placements;
  std::uint64_t placedArea = 0;
  mutable bool objectiveCached = false;
  mutable PolygonObjectiveComponents cachedObjective;
};

/// Сериализуемый результат полного или частичного полигонального поиска.
struct PolygonSolution
{
  std::string problemId;
  SolveStatus status = SolveStatus::InvalidProblem;
  std::vector<PolygonPlacement> placements;
  PolygonObjectiveComponents objective;
  SolverMetrics metrics;
  SolverMetadata solver;
  std::string errorMessage;

  /// Сообщает, является ли решение полным.
  bool complete() const { return status == SolveStatus::Solved; }
};

/// Базовое растровое наблюдение полигонального эпизода.
struct PolygonObservation
{
  int rasterRows = 128;
  int rasterColumns = 128;
  std::vector<float> occupied;
  std::vector<float> clearance;
  std::vector<unsigned char> remaining;
  std::vector<float> partFeatures;
  std::vector<float> objective;
};

/// Условное наблюдение для выбранного экземпляра и поворота.
struct PolygonPlacementObservation
{
  int rasterRows = 128;
  int rasterColumns = 128;
  std::vector<float> channels;
  std::vector<PolygonAction> actions;
  std::vector<float> candidateFeatures;
};
} // namespace aipackaging::solver

#endif
