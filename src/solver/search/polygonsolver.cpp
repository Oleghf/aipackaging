#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <stdexcept>
#include <tuple>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_solver.h>

#ifndef AIPACKAGING_PROJECT_VERSION
#define AIPACKAGING_PROJECT_VERSION "unknown"
#endif

#ifndef AIPACKAGING_BUILD_REVISION
#define AIPACKAGING_BUILD_REVISION "unknown"
#endif

namespace aipackaging::solver
{
namespace
{
using Clock = std::chrono::steady_clock;

/// Стратегия стабильного порядка обязательных экземпляров.
enum class PolygonOrdering : std::uint8_t
{
  Input,
  Area,
  MaxSide
};

/// Накопитель бюджетов, счётчиков и времени полигонального поиска.
struct PolygonSearchContext
{
  const PolygonEnvironment & environment;
  const SolverConfig & config;
  const PolygonExecutionControl & control;
  Clock::time_point started = Clock::now();
  Clock::duration generation{};
  Clock::duration validation{};
  SolverMetrics metrics;
  bool cancellationObserved = false;

  /// Проверяет аварийный timeout, не влияющий на детерминированный quality budget.
  bool timedOut() const { return config.timeoutMs > 0 && Clock::now() - started >= std::chrono::milliseconds(config.timeoutMs); }

  /// Опрашивает внешний источник отмены только на безопасных границах поиска.
  bool cancelled()
  {
    if (control.cancellationRequested && control.cancellationRequested())
      cancellationObserved = true;
    return cancellationObserved;
  }

  /// Передаёт вызывающему коду компактный снимок прогресса.
  void report(PolygonProgressStage stage, std::uint64_t completed, std::uint64_t total, const PolygonState & best) const
  {
    if (control.progress)
      control.progress({stage, completed, total, best.placements.size(), environment.instances().size(), metrics.expandedStates});
  }

  /// Получает динамические кандидаты и учитывает стоимость генерации.
  std::vector<PolygonAction> candidates(const PolygonState & state, std::size_t instance)
  {
    const auto before = Clock::now();
    std::vector<PolygonAction> result = environment.enumerateCandidates(state, instance);
    generation += Clock::now() - before;
    metrics.candidatesGenerated += result.size();
    return result;
  }

  /// Повторно проверяет кандидата и учитывает стоимость validation.
  bool valid(const PolygonState & state, const PolygonAction & action)
  {
    const auto before = Clock::now();
    const bool result = environment.canApply(state, action);
    validation += Clock::now() - before;
    ++metrics.candidatesValidated;
    return result;
  }
};

/// Сравнивает действия для последнего воспроизводимого tie-break.
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

/// Сравнивает состояния через рассчитанный средой objective.
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

/// Строит input/area/max-side порядок с устойчивым ID tie-break.
std::vector<std::size_t> orderedInstances(const PolygonEnvironment & environment, PolygonOrdering ordering)
{
  std::vector<std::size_t> result(environment.instances().size());
  std::iota(result.begin(), result.end(), 0);
  if (ordering == PolygonOrdering::Input)
    return result;
  std::stable_sort(result.begin(), result.end(),
                   [&environment, ordering](std::size_t lhs, std::size_t rhs)
                   {
                     const auto & left = environment.instances()[lhs];
                     const auto & right = environment.instances()[rhs];
                     if (ordering == PolygonOrdering::Area && left.area != right.area)
                       return left.area > right.area;
                     if (left.maxDimension != right.maxDimension)
                       return left.maxDimension > right.maxDimension;
                     if (ordering == PolygonOrdering::MaxSide && left.area != right.area)
                       return left.area > right.area;
                     const std::string & leftId = environment.problem().parts[left.partIndex].id;
                     const std::string & rightId = environment.problem().parts[right.partIndex].id;
                     return std::tie(leftId, left.instanceIndex) < std::tie(rightId, right.instanceIndex);
                   });
  return result;
}

/// Последовательно выбирает первый уже отсортированный допустимый NFP-кандидат.
PolygonState runOrder(PolygonSearchContext & context, const std::vector<std::size_t> & order, bool reportInstances = true)
{
  PolygonState state = context.environment.initialState();
  for (std::size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
  {
    if (context.timedOut() || context.cancelled())
      break;
    const std::size_t instance = order[orderIndex];
    for (const PolygonAction & action : context.candidates(state, instance))
    {
      if (context.cancelled())
        break;
      if (context.valid(state, action))
      {
        context.environment.apply(state, action);
        ++context.metrics.expandedStates;
        break;
      }
    }
    if (reportInstances)
      context.report(PolygonProgressStage::Instances, orderIndex + 1, order.size(), state);
  }
  return state;
}

/// Запускает seeded Fisher-Yates перестановки и сохраняет лучший результат.
PolygonState runRandom(PolygonSearchContext & context, SolveStatus & status)
{
  PolygonState best = context.environment.initialState();
  std::vector<std::size_t> order = orderedInstances(context.environment, PolygonOrdering::Input);
  std::mt19937_64 random(context.config.seed);
  for (std::size_t iteration = 0; iteration < context.config.randomIterations; ++iteration)
  {
    if (context.cancelled())
      return best;
    if (context.timedOut())
    {
      status = SolveStatus::TimedOut;
      return best;
    }
    std::shuffle(order.begin(), order.end(), random);
    PolygonState candidate = runOrder(context, order, false);
    if (betterState(context.environment, candidate, best))
      best = std::move(candidate);
    context.report(PolygonProgressStage::RandomIterations, iteration + 1, context.config.randomIterations, best);
  }
  status = best.placements.size() == context.environment.instances().size() ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
  return best;
}

/// Расширяет совместный выбор детали и позиции слоями ограниченного beam.
PolygonState runBeam(PolygonSearchContext & context, SolveStatus & status)
{
  PolygonState best = context.environment.initialState();
  std::vector<PolygonState> beam{best};
  while (!beam.empty() && best.placements.size() < context.environment.instances().size())
  {
    if (context.cancelled())
      return best;
    std::vector<PolygonState> children;
    bool budget = false;
    for (const PolygonState & state : beam)
    {
      for (std::size_t instance = 0; instance < context.environment.instances().size(); ++instance)
      {
        if (state.placedInstances[instance])
          continue;
        const auto & current = context.environment.instances()[instance];
        bool duplicate = false;
        for (std::size_t earlier = 0; earlier < instance; ++earlier)
          if (!state.placedInstances[earlier] && context.environment.instances()[earlier].partIndex == current.partIndex)
            duplicate = true;
        if (duplicate)
          continue;
        for (const PolygonAction & action : context.candidates(state, instance))
        {
          if (context.cancelled())
            return best;
          if (context.timedOut())
          {
            status = SolveStatus::TimedOut;
            return best;
          }
          if (context.metrics.expandedStates >= context.config.maxExpandedStates)
          {
            budget = true;
            break;
          }
          if (!context.valid(state, action))
            continue;
          PolygonState child = state;
          context.environment.apply(child, action);
          ++context.metrics.expandedStates;
          if (betterState(context.environment, child, best))
            best = child;
          children.push_back(std::move(child));
          context.report(PolygonProgressStage::ExpandedStates, context.metrics.expandedStates, context.config.maxExpandedStates,
                         best);
        }
        if (budget)
          break;
      }
      if (budget)
        break;
    }
    if (budget || children.empty())
      break;
    std::stable_sort(children.begin(), children.end(),
                     [&context](const auto & lhs, const auto & rhs) { return betterState(context.environment, lhs, rhs); });
    if (children.size() > context.config.beamWidth)
      children.resize(context.config.beamWidth);
    beam = std::move(children);
  }
  status = best.placements.size() == context.environment.instances().size()
           ? SolveStatus::Solved
           : (context.metrics.expandedStates >= context.config.maxExpandedStates ? SolveStatus::BudgetExhausted
                                                                                 : SolveStatus::NoSolutionFound);
  return best;
}

/// Создаёт baseline provenance для polygon_solution v1.
SolverMetadata metadata(const SolverConfig & config)
{
  SolverMetadata result;
  result.family = SolverFamily::Baseline;
  result.name = toString(config.solver);
  result.projectVersion = AIPACKAGING_PROJECT_VERSION;
  result.revision = AIPACKAGING_BUILD_REVISION;
  result.seed = config.seed;
  result.randomIterations = config.randomIterations;
  result.beamWidth = config.beamWidth;
  result.maxExpandedStates = config.maxExpandedStates;
  result.timeoutMs = config.timeoutMs;
  return result;
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

/// Валидирует задачу, запускает алгоритм и независимо проверяет собранный результат.
PolygonSolverExecutionResult runPolygonProblem(const PolygonProblem & problem, const SolverConfig & config,
                                               const PolygonExecutionControl & control)
{
  PolygonSolution solution;
  solution.problemId = problem.problemId;
  solution.solver = metadata(config);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
  {
    solution.status = SolveStatus::InvalidProblem;
    solution.errorMessage = error;
    return {std::move(solution), false};
  }
  PolygonSearchContext context{*environment, config, control};
  SolveStatus status = SolveStatus::NoSolutionFound;
  PolygonState state = environment->initialState();
  try
  {
    if (config.solver == SolverKind::RandomLeftBottom)
      state = runRandom(context, status);
    else if (config.solver == SolverKind::Beam)
      state = runBeam(context, status);
    else
    {
      const PolygonOrdering ordering =
        config.solver == SolverKind::InputFirstFit
          ? PolygonOrdering::Input
          : (config.solver == SolverKind::AreaLeftBottom ? PolygonOrdering::Area : PolygonOrdering::MaxSide);
      state = runOrder(context, orderedInstances(*environment, ordering));
      status = context.timedOut() ? SolveStatus::TimedOut
                                  : (state.placements.size() == environment->instances().size() ? SolveStatus::Solved
                                                                                                : SolveStatus::NoSolutionFound);
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
  const auto finished = Clock::now();
  solution.metrics = context.metrics;
  solution.metrics.candidateGenerationTimeUs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(context.generation).count());
  solution.metrics.validationTimeUs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(context.validation).count());
  solution.metrics.totalTimeUs =
    static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(finished - context.started).count());
  solution.metrics.searchTimeUs =
    solution.metrics.totalTimeUs -
    std::min(solution.metrics.totalTimeUs, solution.metrics.candidateGenerationTimeUs + solution.metrics.validationTimeUs);
  const ValidationResult validation = validatePolygonSolution(problem, solution);
  if (!validation.success)
  {
    solution.status = SolveStatus::InvalidProblem;
    solution.errorMessage = "internal polygon solution validation failed: " + validation.error;
  }
  return {std::move(solution), context.cancellationObserved};
}

/// Делегирует обычный синхронный запуск управляемому API без внешнего control.
PolygonSolution solvePolygonProblem(const PolygonProblem & problem, const SolverConfig & config)
{
  return runPolygonProblem(problem, config).solution;
}
} // namespace aipackaging::solver
