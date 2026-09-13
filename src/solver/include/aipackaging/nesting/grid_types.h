#ifndef AIPACKAGING_NESTING_GRID_TYPES_H
#define AIPACKAGING_NESTING_GRID_TYPES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <aipackaging/nesting/common_contracts.h>

namespace aipackaging::solver
{
/// Адрес одной занятой клетки в целочисленной сетке листа.
struct GridCell
{
  int column = 0;
  int row = 0;

  /// Сравнивает адреса клеток по колонке и строке.
  bool operator==(const GridCell &) const = default;
};

/// Размер и единица измерения прямоугольного клеточного листа.
struct GridSheet
{
  int columns = 0;
  int rows = 0;
  std::string unit = "cell";
};

/// Описание типа детали и требуемого количества её экземпляров.
struct GridPart
{
  std::string id;
  std::uint32_t quantity = 1;
  std::vector<GridCell> cells;
  std::vector<int> allowedRotations;
};

/// Совместимое имя общего описания целевой функции для клеточного API.
using GridObjectiveDefinition = ObjectiveDefinition;

/// Полное неизменяемое описание одной задачи клеточного раскроя.
struct GridProblem
{
  std::string problemId;
  GridSheet sheet;
  std::vector<GridPart> parts;
  GridObjectiveDefinition objective;
};

/// Размещение одного экземпляра детали на листе.
struct GridPlacement
{
  std::string partId;
  std::uint32_t instanceIndex = 0;
  int column = 0;
  int row = 0;
  int rotationDegrees = 0;

  /// Сравнивает все поля двух размещений.
  bool operator==(const GridPlacement &) const = default;
};

/// Действие среды совпадает с окончательным описанием размещения.
using GridAction = GridPlacement;

/// Набор независимо сохраняемых компонент оценки текущей раскладки.
struct ObjectiveComponents
{
  int usedLength = 0;
  int primaryRemnantWidth = 0;
  std::size_t largestExtraRectangleArea = 0;
  std::size_t fragmentationPenalty = 0;
  std::size_t placedParts = 0;
  std::size_t totalParts = 0;
  std::size_t placedCells = 0;
  std::size_t totalPartCells = 0;
  double materialUtilization = 0.0;
};

/// Сериализуемый результат, включая допустимое частичное решение.
struct GridSolution
{
  int wireVersion = 1;
  std::string problemId;
  SolveStatus status = SolveStatus::InvalidProblem;
  std::vector<GridPlacement> placements;
  ObjectiveComponents objective;
  SolverMetrics metrics;
  SolverMetadata solver;
  std::string errorMessage;

  /// Сообщает, размещены ли все требуемые экземпляры деталей.
  bool complete() const { return status == SolveStatus::Solved; }
};
} // namespace aipackaging::solver

#endif
