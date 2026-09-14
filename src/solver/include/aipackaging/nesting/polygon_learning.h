#ifndef AIPACKAGING_NESTING_POLYGON_LEARNING_H
#define AIPACKAGING_NESTING_POLYGON_LEARNING_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <aipackaging/nesting/polygon_environment.h>

namespace aipackaging::solver
{
/// Настраивает версию вознаграждения полигональной обучаемой среды.
struct PolygonLearningConfig
{
  int rewardVersion = 1;
};

/// Результат одного перехода динамической полигональной обучаемой среды.
struct PolygonLearningStepResult
{
  PolygonObservation observation;
  double reward = 0.0;
  bool terminated = false;
  bool complete = false;
  bool deadEnd = false;
  int rewardVersion = 1;
  double potentialBefore = 0.0;
  double potentialAfter = 0.0;
  std::vector<double> componentDeltas;
};

/// Результат перехода без повторного копирования неизменной части наблюдения.
struct PolygonLearningCompactStepResult
{
  PolygonDynamicObservation observation;
  double reward = 0.0;
  bool terminated = false;
  bool complete = false;
  bool deadEnd = false;
  int rewardVersion = 1;
  double potentialBefore = 0.0;
  double potentialAfter = 0.0;
  std::vector<double> componentDeltas;
};

/// Пошаговая среда с динамическим каталогом точных полигональных действий.
class PolygonLearningEnvironment
{
public:
  /// Создаёт M4-среду либо возвращает диагностическое сообщение.
  static std::unique_ptr<PolygonLearningEnvironment> Create(const PolygonProblem & problem, std::string & error);
  /// Создаёт среду с явно выбранной совместимой версией вознаграждения.
  static std::unique_ptr<PolygonLearningEnvironment> Create(const PolygonProblem & problem, const PolygonLearningConfig & config,
                                                            std::string & error);

  /// Сбрасывает эпизод и возвращает базовое наблюдение.
  PolygonObservation reset();
  /// Возвращает базовое наблюдение текущего состояния.
  PolygonObservation observation() const;
  /// Возвращает неизменные маски ориентаций и признаки экземпляров.
  PolygonStaticObservation staticObservation() const;
  /// Возвращает изменяемые растры, маски допустимых пар и целевую функцию.
  PolygonDynamicObservation dynamicObservation() const;
  /// Сбрасывает эпизод и возвращает только изменяемую часть наблюдения.
  PolygonDynamicObservation resetCompact();
  /// Возвращает четыре растровых канала и действия выбранной пары экземпляр/поворот.
  PolygonPlacementObservation placementObservation(std::size_t instancePosition, int rotationDegrees) const;
  /// Возвращает текущий динамический каталог всех допустимых действий.
  const std::vector<PolygonAction> & actions() const { return actions_; }
  /// Применяет действие текущего каталога и возвращает переход.
  PolygonLearningStepResult step(std::size_t actionIndex);
  /// Применяет действие и возвращает переход без копирования статического наблюдения.
  PolygonLearningCompactStepResult stepCompact(std::size_t actionIndex);
  /// Возвращает полное либо текущее частичное решение для независимой проверки.
  PolygonSolution solution(const SolverMetadata & metadata = {}) const;
  /// Сообщает, размещены ли все экземпляры.
  bool isComplete() const;
  /// Сообщает, завершён ли эпизод полнотой или отсутствием действий.
  bool isTerminal() const;
  /// Возвращает идентификатор задачи.
  const std::string & problemId() const { return environment_->problem().problemId; }

private:
  /// Принимает владеющую точную среду и строит исходный каталог.
  explicit PolygonLearningEnvironment(std::unique_ptr<PolygonEnvironment> environment, PolygonLearningConfig config);
  /// Перестраивает каталог после изменения состояния.
  void rebuildActions();
  /// Применяет действие и вычисляет общую диагностику перехода.
  PolygonLearningCompactStepResult applyStep(std::size_t actionIndex);

  std::unique_ptr<PolygonEnvironment> environment_;
  PolygonState state_;
  std::vector<PolygonAction> actions_;
  PolygonLearningConfig config_;
};
} // namespace aipackaging::solver

#endif
