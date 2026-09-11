#ifndef AIPACKAGING_NESTING_GRID_LEARNING_H
#define AIPACKAGING_NESTING_GRID_LEARNING_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <aipackaging/nesting/grid_environment.h>

namespace aipackaging::solver
{
/// Ограничения ресурсов одного эпизода обучаемой клеточной среды v1.
struct GridLearningLimits
{
  std::size_t maxSheetArea = 4096;
  std::size_t maxActions = 1000000;
};

/// Плоское наблюдение C++, из которого binding формирует типизированные NumPy-массивы.
struct GridLearningObservation
{
  int rows = 0;
  int columns = 0;
  int maxPartRows = 0;
  int maxPartColumns = 0;
  std::size_t instanceCount = 0;
  std::size_t actionCount = 0;
  std::vector<std::uint8_t> occupancy;
  std::vector<std::uint8_t> partMasks;
  std::vector<std::uint8_t> orientationMask;
  std::vector<std::uint8_t> remaining;
  std::vector<float> partFeatures;
  std::vector<std::int32_t> candidateInstance;
  std::vector<std::uint8_t> candidateRotation;
  std::vector<float> candidateFeatures;
  std::vector<std::uint8_t> actionMask;
  std::vector<float> objective;
};

/// Неизменная часть observation v1, которую достаточно передать Python один раз за эпизод.
struct GridLearningStaticObservation
{
  int rows = 0;
  int columns = 0;
  int maxPartRows = 0;
  int maxPartColumns = 0;
  std::size_t instanceCount = 0;
  std::size_t actionCount = 0;
  std::vector<std::uint8_t> partMasks;
  std::vector<std::uint8_t> orientationMask;
  std::vector<float> partFeatures;
  std::vector<std::int32_t> candidateInstance;
  std::vector<std::uint8_t> candidateRotation;
  std::vector<float> candidateFeatures;
};

/// Изменяемая часть observation v1 после reset или одного допустимого действия.
struct GridLearningDynamicObservation
{
  std::vector<std::uint8_t> occupancy;
  std::vector<std::uint8_t> remaining;
  std::vector<std::uint8_t> actionMask;
  std::vector<float> objective;
};

/// Аудируемые изменения ранга и objective после одного допустимого действия.
struct GridRewardComponents
{
  std::uint64_t rankBefore = 0;
  std::uint64_t rankAfter = 0;
  std::int64_t placedPartsDelta = 0;
  std::int64_t placedCellsDelta = 0;
  int usedLengthDelta = 0;
  std::int64_t largestExtraRectangleAreaDelta = 0;
  std::int64_t fragmentationPenaltyDelta = 0;
};

/// Результат одного шага среды вместе с новым наблюдением и признаками завершения.
struct GridLearningStepResult
{
  GridLearningObservation observation;
  double reward = 0.0;
  bool terminated = false;
  bool complete = false;
  bool deadEnd = false;
  GridRewardComponents rewardComponents;
};

/// Результат шага без повторного копирования неизменного каталога действий.
struct GridLearningCompactStepResult
{
  GridLearningDynamicObservation observation;
  double reward = 0.0;
  bool terminated = false;
  bool complete = false;
  bool deadEnd = false;
  GridRewardComponents rewardComponents;
};

/// Детерминированный эпизод с постоянным action space для обучения и replay.
class GridLearningEnvironment
{
public:
  /// Создаёт среду v1 для валидной M1-задачи либо возвращает диагностическую ошибку.
  static std::unique_ptr<GridLearningEnvironment> Create(const GridProblem & problem, const GridLearningLimits & limits,
                                                         std::string & error);

  /// Возвращает среду в исходное состояние и формирует первое наблюдение.
  GridLearningObservation reset();
  /// Возвращает среду в исходное состояние и формирует только динамическое наблюдение.
  GridLearningDynamicObservation resetCompact();
  /// Возвращает неизменные признаки задачи и постоянного каталога действий.
  GridLearningStaticObservation staticObservation() const;
  /// Возвращает occupancy, remaining, action mask и objective текущего состояния.
  GridLearningDynamicObservation dynamicObservation() const;
  /// Возвращает независимый снимок текущего наблюдения.
  GridLearningObservation observation() const;
  /// Применяет действие по стабильному индексу и возвращает результат перехода.
  GridLearningStepResult step(std::size_t actionIndex);
  /// Применяет действие и возвращает только изменяемую часть observation.
  GridLearningCompactStepResult stepCompact(std::size_t actionIndex);
  /// Формирует независимо проверяемое решение из текущих размещений и provenance вызывающего кода.
  GridSolution snapshotSolution(const SolverMetadata & solver, SolveStatus incompleteStatus = SolveStatus::BudgetExhausted) const;

  /// Возвращает действие стабильного каталога по индексу.
  const GridAction & action(std::size_t actionIndex) const;
  /// Возвращает число действий в неизменном каталоге эпизода.
  std::size_t actionCount() const { return actions_.size(); }
  /// Возвращает число строк листа текущей задачи.
  int rows() const { return staticObservation_.rows; }
  /// Возвращает число столбцов листа текущей задачи.
  int columns() const { return staticObservation_.columns; }
  /// Находит индекс точного публичного действия либо возвращает actionCount().
  std::size_t findAction(const GridAction & action) const;
  /// Сообщает, размещены ли все обязательные экземпляры.
  bool isComplete() const { return complete_; }
  /// Сообщает, нельзя ли больше выполнять шаги эпизода.
  bool isTerminal() const { return terminal_; }
  /// Сообщает, завершился ли неполный эпизод из-за отсутствия действий.
  bool isDeadEnd() const { return terminal_ && !complete_; }
  /// Возвращает текущий точный смешанный ранг reward v1.
  std::uint64_t rank() const;
  /// Возвращает общий нормирующий верхний предел reward v1.
  std::uint64_t rankUpperBound() const { return rankUpperBound_; }
  /// Возвращает идентификатор задачи текущего эпизода.
  const std::string & problemId() const { return environment_->problem().problemId; }

private:
  /// Принимает подготовленную M1-среду, каталог и неизменные признаки наблюдения.
  GridLearningEnvironment(std::unique_ptr<GridEnvironment> environment, GridLearningLimits limits,
                          std::vector<GridAction> actions);

  /// Строит маску допустимых действий для текущего value-state.
  std::vector<std::uint8_t> buildActionMask() const;
  /// Пересчитывает признаки complete/dead-end из текущего состояния и маски.
  void updateTerminal(const std::vector<std::uint8_t> & actionMask);
  /// Вычисляет смешанный ранг reward v1 с проверенной целочисленной арифметикой.
  std::uint64_t calculateRank(const ObjectiveComponents & objective) const;
  /// Создаёт неизменную часть наблюдения: маски фигур и признаки каталога.
  GridLearningObservation buildStaticObservation() const;
  /// Собирает динамическое наблюдение с уже вычисленной маской действий.
  GridLearningDynamicObservation buildDynamicObservation(std::vector<std::uint8_t> actionMask) const;
  /// Проверяет и применяет действие, возвращая общие для полного и compact API данные перехода.
  GridLearningCompactStepResult applyStep(std::size_t actionIndex);

  std::unique_ptr<GridEnvironment> environment_;
  GridLearningLimits limits_;
  std::vector<GridAction> actions_;
  GridState state_;
  GridLearningObservation staticObservation_;
  std::uint64_t rankUpperBound_ = 1;
  bool complete_ = false;
  bool terminal_ = false;
};
} // namespace aipackaging::solver

#endif
