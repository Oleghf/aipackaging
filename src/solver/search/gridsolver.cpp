#include <algorithm>
#include <tuple>
#include <utility>

#include <aipackaging/nesting/grid_environment.h>
#include <aipackaging/nesting/grid_solver.h>

#include "searchalgorithms.h"

namespace aipackaging::solver
{
namespace
{
using detail::PartOrdering;
using detail::PlacementPolicy;
using detail::SearchRuntime;
using detail::SearchStopReason;

/// Задаёт стабильный tie-break двух grid-размещений независимо от порядка в памяти.
bool placementLess(const GridPlacement & lhs, const GridPlacement & rhs)
{
  return std::tie(lhs.partId, lhs.instanceIndex, lhs.rotationDegrees, lhs.column, lhs.row) <
         std::tie(rhs.partId, rhs.instanceIndex, rhs.rotationDegrees, rhs.column, rhs.row);
}

/// Сравнивает grid-состояния по полноте, объёму partial, objective и последовательности действий.
bool betterState(const GridEnvironment & environment, const GridState & lhs, const GridState & rhs)
{
  const ObjectiveComponents left = environment.evaluate(lhs);
  const ObjectiveComponents right = environment.evaluate(rhs);
  const bool leftComplete = left.placedParts == left.totalParts;
  const bool rightComplete = right.placedParts == right.totalParts;
  if (leftComplete != rightComplete)
    return leftComplete;
  if (left.placedParts != right.placedParts)
    return left.placedParts > right.placedParts;
  if (left.placedCells != right.placedCells)
    return left.placedCells > right.placedCells;
  if (left.usedLength != right.usedLength)
    return left.usedLength < right.usedLength;
  if (left.largestExtraRectangleArea != right.largestExtraRectangleArea)
    return left.largestExtraRectangleArea > right.largestExtraRectangleArea;
  if (left.fragmentationPenalty != right.fragmentationPenalty)
    return left.fragmentationPenalty < right.fragmentationPenalty;
  return std::lexicographical_compare(lhs.placements.begin(), lhs.placements.end(), rhs.placements.begin(), rhs.placements.end(),
                                      placementLess);
}

/// Сравнивает допустимые grid-кандидаты по valuable-remnant left-bottom.
bool leftBottomLess(const GridEnvironment & environment, const GridState & state, std::size_t instancePosition,
                    const GridAction & lhs, const GridAction & rhs)
{
  const GridPartInstance & instance = environment.instances()[instancePosition];
  const GridOrientation * leftOrientation = environment.findOrientation(instance.partIndex, lhs.rotationDegrees);
  const GridOrientation * rightOrientation = environment.findOrientation(instance.partIndex, rhs.rotationDegrees);
  const int currentUsedLength = environment.evaluate(state).usedLength;
  const int leftUsedLength = std::max(currentUsedLength, lhs.column + leftOrientation->width);
  const int rightUsedLength = std::max(currentUsedLength, rhs.column + rightOrientation->width);
  if (leftUsedLength != rightUsedLength)
    return leftUsedLength < rightUsedLength;
  const int leftBottom = lhs.row + leftOrientation->height;
  const int rightBottom = rhs.row + rightOrientation->height;
  if (leftBottom != rightBottom)
    return leftBottom > rightBottom;
  if (lhs.column != rhs.column)
    return lhs.column < rhs.column;
  if (lhs.rotationDegrees != rhs.rotationDegrees)
    return lhs.rotationDegrees < rhs.rotationDegrees;
  return placementLess(lhs, rhs);
}

/// Адаптирует GridEnvironment к общему lifecycle без переноса grid-геометрии в runtime.
class GridSearchAdapter
{
public:
  using State = GridState;
  using Action = GridAction;

  /// Создаёт адаптер над средой, принадлежащей внешнему фасаду запуска.
  explicit GridSearchAdapter(const GridEnvironment & environment)
    : environment_(environment)
  {
  }

  /// Возвращает пустое состояние grid-среды.
  State initialState() const { return environment_.initialState(); }
  /// Возвращает число обязательных экземпляров задачи.
  std::size_t instanceCount() const { return environment_.instances().size(); }
  /// Возвращает площадь экземпляра в клетках для общего ordering.
  std::size_t instanceArea(std::size_t index) const { return environment_.instances()[index].area; }
  /// Возвращает максимальный габарит экземпляра для общего ordering.
  int instanceMaxDimension(std::size_t index) const { return environment_.instances()[index].maxDimension; }
  /// Возвращает стабильный ID типа детали для общего ordering.
  const std::string & instancePartId(std::size_t index) const
  {
    return environment_.problem().parts[environment_.instances()[index].partIndex].id;
  }
  /// Возвращает публичный индекс экземпляра внутри типа детали.
  std::uint32_t instanceIndex(std::size_t index) const { return environment_.instances()[index].instanceIndex; }
  /// Сообщает, размещён ли экземпляр в переданном value-state.
  bool isPlaced(const State & state, std::size_t index) const { return state.placedInstances[index] != 0; }
  /// Сообщает, принадлежат ли два экземпляра одному типу детали.
  bool samePart(std::size_t lhs, std::size_t rhs) const
  {
    return environment_.instances()[lhs].partIndex == environment_.instances()[rhs].partIndex;
  }
  /// Возвращает количество размещений текущего состояния.
  std::size_t placedCount(const State & state) const { return state.placements.size(); }
  /// Сообщает, содержит ли состояние все обязательные экземпляры.
  bool complete(const State & state) const { return state.placements.size() == instanceCount(); }
  /// Возвращает детерминированный каталог кандидатов выбранного экземпляра.
  std::vector<Action> candidates(const State & state, std::size_t instance) const
  {
    return environment_.enumerateCandidates(state, instance);
  }
  /// Проверяет один grid-кандидат точными правилами среды.
  bool valid(const State & state, const Action & action) const { return environment_.canApply(state, action); }
  /// Применяет уже проверенный grid-кандидат к копии состояния.
  void apply(State & state, const Action & action) const { environment_.apply(state, action); }
  /// Сравнивает два состояния по принятому grid objective.
  bool better(const State & candidate, const State & reference) const { return betterState(environment_, candidate, reference); }
  /// Сохраняет прежнюю grid-семантику проверки beam budget после validation.
  bool budgetBeforeValidation() const { return false; }

  /// Выбирает и применяет first-fit либо лучший left-bottom кандидат одного экземпляра.
  bool placeOrdered(SearchRuntime & runtime, State & state, std::size_t instance, PlacementPolicy policy) const
  {
    const auto generationStarted = runtime.now();
    const std::vector<Action> actions = candidates(state, instance);
    runtime.recordCandidateGeneration(generationStarted, actions.size());
    bool found = false;
    Action best;
    for (const Action & action : actions)
    {
      if (runtime.pollStop())
        return false;
      const auto validationStarted = runtime.now();
      const bool applicable = valid(state, action);
      runtime.recordValidation(validationStarted);
      if (!applicable)
        continue;
      if (policy == PlacementPolicy::FirstFit)
      {
        apply(state, action);
        runtime.recordExpansion();
        return true;
      }
      if (!found || leftBottomLess(environment_, state, instance, action, best))
      {
        best = action;
        found = true;
      }
    }
    if (found)
    {
      apply(state, best);
      runtime.recordExpansion();
    }
    return found;
  }

private:
  const GridEnvironment & environment_;
};

/// Преобразует SolverKind в общий ordering и placement policy последовательного поиска.
std::pair<PartOrdering, PlacementPolicy> orderedPolicy(SolverKind solver)
{
  if (solver == SolverKind::InputFirstFit)
    return {PartOrdering::Input, PlacementPolicy::FirstFit};
  if (solver == SolverKind::AreaLeftBottom)
    return {PartOrdering::AreaDescending, PlacementPolicy::LeftBottom};
  return {PartOrdering::MaxSideDescending, PlacementPolicy::LeftBottom};
}
} // namespace

/// Сравнивает полноту, объём partial, остаток и стабильную последовательность placements.
bool isBetterGridSolution(const GridSolution & candidate, const GridSolution & reference)
{
  const bool candidateComplete = candidate.complete();
  const bool referenceComplete = reference.complete();
  if (candidateComplete != referenceComplete)
    return candidateComplete;
  if (candidate.objective.placedParts != reference.objective.placedParts)
    return candidate.objective.placedParts > reference.objective.placedParts;
  if (candidate.objective.placedCells != reference.objective.placedCells)
    return candidate.objective.placedCells > reference.objective.placedCells;
  if (candidate.objective.usedLength != reference.objective.usedLength)
    return candidate.objective.usedLength < reference.objective.usedLength;
  if (candidate.objective.largestExtraRectangleArea != reference.objective.largestExtraRectangleArea)
    return candidate.objective.largestExtraRectangleArea > reference.objective.largestExtraRectangleArea;
  if (candidate.objective.fragmentationPenalty != reference.objective.fragmentationPenalty)
    return candidate.objective.fragmentationPenalty < reference.objective.fragmentationPenalty;
  return std::lexicographical_compare(candidate.placements.begin(), candidate.placements.end(), reference.placements.begin(),
                                      reference.placements.end(), placementLess);
}

/// Проверяет задачу, запускает общий lifecycle через grid-адаптер и собирает проверенный результат.
GridSolverExecutionResult runGridProblem(const GridProblem & problem, const SolverConfig & config,
                                         const SearchExecutionControl & control)
{
  GridSolution solution;
  solution.problemId = problem.problemId;
  solution.solver = detail::makeBaselineMetadata(config);

  if ((config.solver == SolverKind::RandomLeftBottom && config.randomIterations == 0) ||
      (config.solver == SolverKind::Beam && (config.beamWidth == 0 || config.maxExpandedStates == 0)))
  {
    solution.errorMessage = "solver budgets must be positive";
    return {std::move(solution), false};
  }

  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  if (!environment)
  {
    solution.errorMessage = error;
    return {std::move(solution), false};
  }

  SearchRuntime runtime(config, control);
  const GridSearchAdapter adapter(*environment);
  SolveStatus status = SolveStatus::NoSolutionFound;
  GridState state;
  if (config.solver == SolverKind::RandomLeftBottom)
    state = detail::runRandom(runtime, adapter, status);
  else if (config.solver == SolverKind::Beam)
    state = detail::runBeam(runtime, adapter, status);
  else
  {
    const auto [ordering, policy] = orderedPolicy(config.solver);
    state = detail::runOrdered(runtime, adapter, detail::orderedInstances(adapter, ordering), policy);
    status = runtime.stopReason() == SearchStopReason::TimedOut
             ? SolveStatus::TimedOut
             : (adapter.complete(state) ? SolveStatus::Solved : SolveStatus::NoSolutionFound);
  }

  solution.status = status;
  solution.objective = environment->evaluate(state);
  solution.placements = std::move(state.placements);
  solution.metrics = runtime.finalizedMetrics();
  if (!solution.complete())
    solution.errorMessage = toString(status);

  // Публичный результат повторно проходит независимый валидатор, поэтому
  // ошибка сборки solution не может выйти за границу Search.
  const ValidationResult validation = validateGridSolution(problem, solution);
  if (!validation.success)
  {
    solution.status = SolveStatus::InvalidProblem;
    solution.errorMessage = "internal grid solution validation failed: " + validation.error;
  }
  return {std::move(solution), runtime.cancellationObserved()};
}

/// Делегирует обычный синхронный запуск управляемому API без внешних callback.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config)
{
  return runGridProblem(problem, config).solution;
}
} // namespace aipackaging::solver
