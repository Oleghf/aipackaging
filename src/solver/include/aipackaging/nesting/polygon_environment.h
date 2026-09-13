#ifndef AIPACKAGING_NESTING_POLYGON_ENVIRONMENT_H
#define AIPACKAGING_NESTING_POLYGON_ENVIRONMENT_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <aipackaging/nesting/polygon_types.h>

namespace aipackaging::solver
{
/// Детерминированная среда раскроя нормализованных полигональных деталей.
class PolygonEnvironment
{
public:
  /// Проверяет исходную задачу, аппроксимирует кривые и создаёт среду либо возвращает ошибку.
  static std::unique_ptr<PolygonEnvironment> Create(const PolygonProblem & problem, std::string & error);

  /// Возвращает исходную проверенную задачу.
  const PolygonProblem & problem() const { return problem_; }
  /// Возвращает ширину листа в микронах.
  std::int64_t sheetWidth() const { return sheetWidth_; }
  /// Возвращает высоту листа в микронах.
  std::int64_t sheetHeight() const { return sheetHeight_; }
  /// Возвращает отступ от края листа в микронах.
  std::int64_t sheetMargin() const { return sheetMargin_; }
  /// Возвращает минимальный междетальный зазор в микронах.
  std::int64_t partSpacing() const { return partSpacing_; }
  /// Возвращает стабильный список обязательных экземпляров.
  const std::vector<PolygonPartInstance> & instances() const { return instances_; }
  /// Возвращает уникальные ориентации выбранного типа детали.
  const std::vector<PolygonOrientation> & orientations(std::size_t partIndex) const;

  /// Создаёт пустое начальное состояние.
  PolygonState initialState() const;
  /// Возвращает динамические контактные кандидаты для одного экземпляра.
  std::vector<PolygonAction> enumerateCandidates(const PolygonState & state, std::size_t instancePosition) const;
  /// Проверяет действие по экземпляру, повороту, границам, пересечениям и зазору.
  bool canApply(const PolygonState & state, const PolygonAction & action) const;
  /// Применяет допустимое действие к переданному изменяемому состоянию.
  bool apply(PolygonState & state, const PolygonAction & action) const;
  /// Вычисляет точные основные и растровые вторичные компоненты целевой функции.
  PolygonObjectiveComponents evaluate(const PolygonState & state) const;

  /// Находит позицию экземпляра по partId и instanceIndex либо возвращает размер списка.
  std::size_t findInstance(const std::string & partId, std::uint32_t instanceIndex) const;
  /// Находит нормализованную ориентацию типа детали либо возвращает `nullptr`.
  const PolygonOrientation * findOrientation(std::size_t partIndex, int rotationDegrees) const;
  /// Возвращает перенесённое внешнее кольцо указанного размещения.
  PolygonRing64 placedOuter(const PolygonPlacement & placement) const;

private:
  /// Сохраняет проверенную задачу и подготовленные геометрические индексы.
  PolygonEnvironment(PolygonProblem problem, std::int64_t sheetWidth, std::int64_t sheetHeight, std::int64_t sheetMargin,
                     std::int64_t partSpacing, std::vector<std::vector<PolygonOrientation>> orientations,
                     std::vector<PolygonPartInstance> instances);

  PolygonProblem problem_;
  std::int64_t sheetWidth_ = 0;
  std::int64_t sheetHeight_ = 0;
  std::int64_t sheetMargin_ = 0;
  std::int64_t partSpacing_ = 0;
  std::vector<std::vector<PolygonOrientation>> orientations_;
  std::vector<PolygonPartInstance> instances_;
};

/// Проверяет исходные поля и производственные ограничения `polygon_problem` v1.
ValidationResult validatePolygonProblem(const PolygonProblem & problem);
/// Независимо воспроизводит полигональную раскладку и сверяет записанную целевую функцию.
ValidationResult validatePolygonSolution(const PolygonProblem & problem, const PolygonSolution & solution);
} // namespace aipackaging::solver

#endif
