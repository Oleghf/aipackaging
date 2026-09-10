#ifndef AIPACKAGING_SOLVER_GRIDTYPES_H
#define AIPACKAGING_SOLVER_GRIDTYPES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

/// Идентификатор поддерживаемой целевой функции задачи.
struct GridObjectiveDefinition
{
  std::string type = "valuable_right_remnant";
  int version = 1;
};

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

/// Итоговый статус попытки решить задачу.
enum class SolveStatus : std::uint8_t
{
  Solved,
  NoSolutionFound,
  BudgetExhausted,
  TimedOut,
  InvalidProblem
};

/// Доступные реализации клеточных baseline-решателей.
enum class SolverKind : std::uint8_t
{
  InputFirstFit,
  AreaLeftBottom,
  MaxSideLeftBottom,
  RandomLeftBottom,
  Beam
};

/// Определяет происхождение решения без привязки к конкретному алгоритму.
enum class SolverFamily : std::uint8_t
{
  Baseline,
  Neural,
  Hybrid
};

/// Воспроизводимые настройки выбранного решателя и его ограничений.
struct SolverConfig
{
  SolverKind solver = SolverKind::AreaLeftBottom;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50000;
  std::uint64_t timeoutMs = 30000;
};

/// Счётчики работы решателя и раздельные временные измерения.
struct SolverMetrics
{
  std::uint64_t candidatesGenerated = 0;
  std::uint64_t candidatesValidated = 0;
  std::uint64_t expandedStates = 0;
  std::uint64_t candidateGenerationTimeUs = 0;
  std::uint64_t validationTimeUs = 0;
  std::uint64_t searchTimeUs = 0;
  std::uint64_t totalTimeUs = 0;
};

/// Настройки и версия кода, которыми было получено решение.
struct SolverMetadata
{
  SolverFamily family = SolverFamily::Baseline;
  std::string name;
  std::string projectVersion;
  std::string revision;
  std::uint64_t seed = 42;
  std::size_t randomIterations = 64;
  std::size_t beamWidth = 32;
  std::size_t maxExpandedStates = 50000;
  std::uint64_t timeoutMs = 30000;
  std::string modelId;
  std::string modelSha256;
  std::size_t rollouts = 0;
  std::string selectionMode;
};

/// Сериализуемый результат решения, включая допустимый partial.
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

/// Результат проверки публичного контракта с диагностикой ошибки.
struct ValidationResult
{
  bool success = false;
  std::string error;
};

/// Возвращает стабильное строковое имя статуса для JSON и CLI.
std::string toString(SolveStatus status);
/// Возвращает стабильное строковое имя алгоритма для JSON и CLI.
std::string toString(SolverKind solver);
/// Возвращает стабильное строковое имя семейства решателя для solution v2.
std::string toString(SolverFamily family);
/// Преобразует публичное строковое имя в статус решения.
bool parseSolveStatus(const std::string & value, SolveStatus & status);
/// Преобразует публичное строковое имя в вид решателя.
bool parseSolverKind(const std::string & value, SolverKind & solver);
/// Преобразует публичное строковое имя в семейство решателя.
bool parseSolverFamily(const std::string & value, SolverFamily & family);
} // namespace aipackaging::solver

#endif
