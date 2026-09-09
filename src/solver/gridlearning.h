#ifndef AIPACKAGING_SOLVER_GRIDLEARNING_H
#define AIPACKAGING_SOLVER_GRIDLEARNING_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gridenvironment.h>

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

/// Детерминированный эпизод с постоянным action space для обучения и replay.
class GridLearningEnvironment
{
public:
  /// Создаёт среду v1 для валидной M1-задачи либо возвращает диагностическую ошибку.
  static std::unique_ptr<GridLearningEnvironment> Create(const GridProblem & problem, const GridLearningLimits & limits,
                                                         std::string & error);

  /// Возвращает среду в исходное состояние и формирует первое наблюдение.
  GridLearningObservation reset();
  /// Возвращает независимый снимок текущего наблюдения.
  GridLearningObservation observation() const;
  /// Применяет действие по стабильному индексу и возвращает результат перехода.
  GridLearningStepResult step(std::size_t actionIndex);

  /// Возвращает действие стабильного каталога по индексу.
  const GridAction & action(std::size_t actionIndex) const;
  /// Возвращает число действий в неизменном каталоге эпизода.
  std::size_t actionCount() const { return actions_.size(); }
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
