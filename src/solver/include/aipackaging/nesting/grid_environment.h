#ifndef AIPACKAGING_NESTING_GRID_ENVIRONMENT_H
#define AIPACKAGING_NESTING_GRID_ENVIRONMENT_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <aipackaging/nesting/grid_types.h>

namespace aipackaging::solver
{
/// Нормализованная уникальная ориентация клеточной детали.
struct GridOrientation
{
  int rotationDegrees = 0;
  int width = 0;
  int height = 0;
  std::vector<GridCell> cells;
};

/// Стабильная ссылка на один требуемый экземпляр детали.
struct GridPartInstance
{
  std::size_t partIndex = 0;
  std::uint32_t instanceIndex = 0;
  std::size_t area = 0;
  int maxDimension = 0;
};

/// Value-state среды: занятость листа и уже применённые действия.
struct GridState
{
  std::vector<unsigned char> occupancy;
  std::vector<unsigned char> placedInstances;
  std::vector<GridPlacement> placements;
  std::size_t placedCells = 0;
};

/// Детерминированная клеточная среда, общая для всех решателей и валидатора.
class GridEnvironment
{
public:
  /// Проверяет задачу и создаёт готовую среду либо возвращает описание ошибки.
  static std::unique_ptr<GridEnvironment> Create(const GridProblem & problem, std::string & error);

  /// Возвращает нормализованную задачу среды.
  const GridProblem & problem() const { return problem_; }
  /// Возвращает стабильный список всех требуемых экземпляров.
  const std::vector<GridPartInstance> & instances() const { return instances_; }
  /// Возвращает уникальные допустимые ориентации заданного типа детали.
  const std::vector<GridOrientation> & orientations(std::size_t partIndex) const;

  /// Создаёт пустое состояние для исходного листа.
  GridState initialState() const;
  /// Перечисляет все геометрически помещающиеся на лист кандидаты экземпляра.
  std::vector<GridAction> enumerateCandidates(const GridState & state, std::size_t instancePosition) const;
  /// Проверяет действие относительно границ, занятости и уже использованных экземпляров.
  bool canApply(const GridState & state, const GridAction & action) const;
  /// Применяет допустимое действие к состоянию и сообщает об успехе.
  bool apply(GridState & state, const GridAction & action) const;
  /// Вычисляет все компоненты целевой функции для состояния.
  ObjectiveComponents evaluate(const GridState & state) const;

  /// Находит позицию экземпляра по публичной паре partId/instanceIndex.
  std::size_t findInstance(const std::string & partId, std::uint32_t instanceIndex) const;
  /// Находит сохранённую уникальную ориентацию детали по углу.
  const GridOrientation * findOrientation(std::size_t partIndex, int rotationDegrees) const;

private:
  /// Сохраняет уже проверенную задачу и подготовленные геометрические индексы.
  GridEnvironment(GridProblem problem, std::vector<std::vector<GridOrientation>> orientations,
                  std::vector<GridPartInstance> instances);

  GridProblem problem_;
  std::vector<std::vector<GridOrientation>> orientations_;
  std::vector<GridPartInstance> instances_;
};

/// Проверяет структуру и геометрические инварианты задачи grid_problem v1.
ValidationResult validateGridProblem(const GridProblem & problem);
/// Независимо проверяет placements и записанную оценку решения.
ValidationResult validateGridSolution(const GridProblem & problem, const GridSolution & solution);
} // namespace aipackaging::solver

#endif
