#ifndef AIPACKAGING_SOLVER_POLYGONLEARNING_H
#define AIPACKAGING_SOLVER_POLYGONLEARNING_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <polygonenvironment.h>

namespace aipackaging::solver
{
/// Результат одного перехода динамической полигональной обучаемой среды.
struct PolygonLearningStepResult
{
  PolygonObservation observation;
  double reward = 0.0;
  bool terminated = false;
  bool complete = false;
  bool deadEnd = false;
};

/// Пошаговая среда с динамическим каталогом точных полигональных действий.
class PolygonLearningEnvironment
{
public:
  /// Создаёт M4-среду либо возвращает диагностическое сообщение.
  static std::unique_ptr<PolygonLearningEnvironment> Create(const PolygonProblem & problem, std::string & error);

  /// Сбрасывает эпизод и возвращает базовое наблюдение.
  PolygonObservation reset();
  /// Возвращает базовое наблюдение текущего состояния.
  PolygonObservation observation() const;
  /// Возвращает четыре растровых канала и действия выбранной пары экземпляр/поворот.
  PolygonPlacementObservation placementObservation(std::size_t instancePosition, int rotationDegrees) const;
  /// Возвращает текущий динамический каталог всех допустимых действий.
  const std::vector<PolygonAction> & actions() const { return actions_; }
  /// Применяет действие текущего каталога и возвращает переход.
  PolygonLearningStepResult step(std::size_t actionIndex);
  /// Возвращает полный либо текущий partial solution для независимого аудита.
  PolygonSolution solution(const SolverMetadata & metadata = {}) const;
  /// Сообщает, размещены ли все экземпляры.
  bool isComplete() const;
  /// Сообщает, завершён ли эпизод полнотой или отсутствием действий.
  bool isTerminal() const;
  /// Возвращает идентификатор задачи.
  const std::string & problemId() const { return environment_->problem().problemId; }

private:
  /// Принимает владеющую точную среду и строит исходный каталог.
  explicit PolygonLearningEnvironment(std::unique_ptr<PolygonEnvironment> environment);
  /// Перестраивает каталог после изменения value-state.
  void rebuildActions();

  std::unique_ptr<PolygonEnvironment> environment_;
  PolygonState state_;
  std::vector<PolygonAction> actions_;
};
} // namespace aipackaging::solver

#endif
