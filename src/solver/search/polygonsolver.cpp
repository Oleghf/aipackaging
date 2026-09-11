#include <algorithm>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_solver.h>

#include "searchalgorithms.h"

namespace aipackaging::solver
{
namespace
{
using detail::PartOrdering;
using detail::PlacementPolicy;
using detail::SearchRuntime;
using detail::SearchStopReason;

/// Сравнивает polygon-размещения для последнего воспроизводимого tie-break.
bool actionLess(const PolygonPlacement & lhs, const PolygonPlacement & rhs)
{
  return std::tie(lhs.partId, lhs.instanceIndex, lhs.rotationDegrees, lhs.x, lhs.y) <
         std::tie(rhs.partId, rhs.instanceIndex, rhs.rotationDegrees, rhs.x, rhs.y);
}

/// Быстро находит точный usedLength без вычисления растровых secondary-метрик.
std::int64_t usedLength(const PolygonEnvironment & environment, const PolygonState & state)
{
  std::int64_t right = environment.sheetMargin();
  for (const PolygonPlacement & placement : state.placements)
    for (const PolygonPoint64 & point : environment.placedOuter(placement))
      right = std::max(right, point.x);
  return state.placements.empty() ? 0 : right - environment.sheetMargin();
}

/// Сравнивает polygon-состояния по полноте, partial-полезности, objective и placements.
bool betterState(const PolygonEnvironment & environment, const PolygonState & lhs, const PolygonState & rhs)
{
  const bool leftComplete = lhs.placements.size() == environment.instances().size();
  const bool rightComplete = rhs.placements.size() == environment.instances().size();
  if (leftComplete != rightComplete)
    return leftComplete;
  if (lhs.placements.size() != rhs.placements.size())
    return lhs.placements.size() > rhs.placements.size();
  if (lhs.placedArea != rhs.placedArea)
    return lhs.placedArea > rhs.placedArea;
  const std::int64_t leftUsed = usedLength(environment, lhs);
  const std::int64_t rightUsed = usedLength(environment, rhs);
  if (leftUsed != rightUsed)
    return leftUsed < rightUsed;
  const PolygonObjectiveComponents left = environment.evaluate(lhs);
  const PolygonObjectiveComponents right = environment.evaluate(rhs);
  if (left.largestExtraRectangleArea != right.largestExtraRectangleArea)
    return left.largestExtraRectangleArea > right.largestExtraRectangleArea;
  if (left.fragmentationPenalty != right.fragmentationPenalty)
    return left.fragmentationPenalty < right.fragmentationPenalty;
  return std::lexicographical_compare(lhs.placements.begin(), lhs.placements.end(), rhs.placements.begin(), rhs.placements.end(),
                                      actionLess);
}

/// Адаптирует PolygonEnvironment к общему lifecycle, сохраняя динамический NFP action space.
class PolygonSearchAdapter
{
public:
  using State = PolygonState;
  using Action = PolygonAction;

  /// Создаёт адаптер над нормализованной полигональной средой внешнего фасада.
  explicit PolygonSearchAdapter(const PolygonEnvironment & environment)
    : environment_(environment)
  {
  }

  /// Возвращает пустое состояние polygon-среды.
  State initialState() const { return environment_.initialState(); }
  /// Возвращает число обязательных полигональных экземпляров.
  std::size_t instanceCount() const { return environment_.instances().size(); }
  /// Возвращает точную площадь экземпляра в квадратных микронах для ordering.
  std::uint64_t instanceArea(std::size_t index) const { return environment_.instances()[index].area; }
  /// Возвращает максимальный габарит экземпляра в микронах для ordering.
  std::int64_t instanceMaxDimension(std::size_t index) const { return environment_.instances()[index].maxDimension; }
  /// Возвращает стабильный ID типа детали для общего ordering.
  const std::string & instancePartId(std::size_t index) const
  {
    return environment_.problem().parts[environment_.instances()[index].partIndex].id;
  }
  /// Возвращает публичный индекс экземпляра внутри типа детали.
  std::uint32_t instanceIndex(std::size_t index) const { return environment_.instances()[index].instanceIndex; }
  /// Сообщает, размещён ли экземпляр в переданном polygon state.
  bool isPlaced(const State & state, std::size_t index) const { return state.placedInstances[index] != 0; }
  /// Сообщает, принадлежат ли два экземпляра одному полигональному типу.
  bool samePart(std::size_t lhs, std::size_t rhs) const
  {
    return environment_.instances()[lhs].partIndex == environment_.instances()[rhs].partIndex;
  }
  /// Возвращает количество размещений текущего состояния.
  std::size_t placedCount(const State & state) const { return state.placements.size(); }
  /// Сообщает, содержит ли состояние все обязательные экземпляры.
  bool complete(const State & state) const { return state.placements.size() == instanceCount(); }
  /// Строит динамический NFP-каталог выбранного экземпляра для текущего состояния.
  std::vector<Action> candidates(const State & state, std::size_t instance) const
  {
    return environment_.enumerateCandidates(state, instance);
  }
  /// Проверяет NFP-кандидат точными правилами границ, пересечений и clearance.
  bool valid(const State & state, const Action & action) const { return environment_.canApply(state, action); }
  /// Применяет уже проверенный polygon-кандидат к копии состояния.
  void apply(State & state, const Action & action) const { environment_.apply(state, action); }
  /// Сравнивает состояния по принятому polygon objective.
  bool better(const State & candidate, const State & reference) const { return betterState(environment_, candidate, reference); }
  /// Сохраняет прежнюю polygon-семантику проверки beam budget до validation.
  bool budgetBeforeValidation() const { return true; }

  /// Применяет первый допустимый кандидат уже отсортированного NFP-каталога.
  bool placeOrdered(SearchRuntime & runtime, State & state, std::size_t instance, PlacementPolicy) const
  {
    const auto generationStarted = runtime.now();
    const std::vector<Action> actions = candidates(state, instance);
    runtime.recordCandidateGeneration(generationStarted, actions.size());
    // PolygonEnvironment уже ранжирует кандидаты по usedLength/Y/X/rotation,
    // поэтому дополнительная сортировка в search-слое изменила бы action order.
    for (const Action & action : actions)
    {
      if (runtime.pollStop())
        return false;
      const auto validationStarted = runtime.now();
      const bool applicable = valid(state, action);
      runtime.recordValidation(validationStarted);
      if (!applicable)
        continue;
      apply(state, action);
      runtime.recordExpansion();
      return true;
    }
    return false;
  }

private:
  const PolygonEnvironment & environment_;
};

/// Преобразует SolverKind в общий ordering последовательного polygon-поиска.
PartOrdering orderedPolicy(SolverKind solver)
{
  if (solver == SolverKind::InputFirstFit)
    return PartOrdering::Input;
  if (solver == SolverKind::AreaLeftBottom)
    return PartOrdering::AreaDescending;
  return PartOrdering::MaxSideDescending;
}
} // namespace

/// Сравнивает полноту, partial-полезность, остаток и placements.
bool isBetterPolygonSolution(const PolygonSolution & candidate, const PolygonSolution & reference)
{
  if (candidate.complete() != reference.complete())
    return candidate.complete();
  if (candidate.objective.placedParts != reference.objective.placedParts)
    return candidate.objective.placedParts > reference.objective.placedParts;
  if (candidate.objective.placedArea != reference.objective.placedArea)
    return candidate.objective.placedArea > reference.objective.placedArea;
  if (candidate.objective.usedLength != reference.objective.usedLength)
    return candidate.objective.usedLength < reference.objective.usedLength;
  if (candidate.objective.largestExtraRectangleArea != reference.objective.largestExtraRectangleArea)
    return candidate.objective.largestExtraRectangleArea > reference.objective.largestExtraRectangleArea;
  if (candidate.objective.fragmentationPenalty != reference.objective.fragmentationPenalty)
    return candidate.objective.fragmentationPenalty < reference.objective.fragmentationPenalty;
  return std::lexicographical_compare(candidate.placements.begin(), candidate.placements.end(), reference.placements.begin(),
                                      reference.placements.end(), actionLess);
}

/// Проверяет задачу, запускает общий lifecycle через polygon-адаптер и независимо валидирует результат.
PolygonSolverExecutionResult runPolygonProblem(const PolygonProblem & problem, const SolverConfig & config,
                                               const PolygonExecutionControl & control)
{
  PolygonSolution solution;
  solution.problemId = problem.problemId;
  solution.solver = detail::makeBaselineMetadata(config);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
  {
    solution.status = SolveStatus::InvalidProblem;
    solution.errorMessage = error;
    return {std::move(solution), false};
  }

  SearchRuntime runtime(config, control);
  const PolygonSearchAdapter adapter(*environment);
  SolveStatus status = SolveStatus::NoSolutionFound;
  PolygonState state = environment->initialState();
  try
  {
    if (config.solver == SolverKind::RandomLeftBottom)
      state = detail::runRandom(runtime, adapter, status);
    else if (config.solver == SolverKind::Beam)
      state = detail::runBeam(runtime, adapter, status);
    else
    {
      const PartOrdering ordering = orderedPolicy(config.solver);
      state = detail::runOrdered(runtime, adapter, detail::orderedInstances(adapter, ordering), PlacementPolicy::LeftBottom);
      status = runtime.stopReason() == SearchStopReason::TimedOut
               ? SolveStatus::TimedOut
               : (adapter.complete(state) ? SolveStatus::Solved : SolveStatus::NoSolutionFound);
    }
  }
  catch (const std::length_error & exception)
  {
    status = SolveStatus::UnsupportedEnvironment;
    solution.errorMessage = exception.what();
  }

  solution.status = status;
  solution.placements = state.placements;
  solution.objective = environment->evaluate(state);
  solution.metrics = runtime.finalizedMetrics();
  const ValidationResult validation = validatePolygonSolution(problem, solution);
  if (!validation.success)
  {
    solution.status = SolveStatus::InvalidProblem;
    solution.errorMessage = "internal polygon solution validation failed: " + validation.error;
  }
  return {std::move(solution), runtime.cancellationObserved()};
}

/// Делегирует обычный синхронный запуск управляемому API без внешних callback.
PolygonSolution solvePolygonProblem(const PolygonProblem & problem, const SolverConfig & config)
{
  return runPolygonProblem(problem, config).solution;
}
} // namespace aipackaging::solver
