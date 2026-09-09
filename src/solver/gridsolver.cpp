#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <tuple>
#include <utility>

#include <gridenvironment.h>
#include <gridsolver.h>

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

/// Независимая стратегия определения последовательности экземпляров.
enum class PartOrdering : std::uint8_t
{
  Input,
  AreaDescending,
  MaxSideDescending
};

/// Независимая стратегия выбора размещения для уже выбранного экземпляра.
enum class PlacementPolicy : std::uint8_t
{
  FirstFit,
  LeftBottom
};

/// Приводит длительность steady_clock к целым микросекундам для метрик.
std::uint64_t microseconds(Clock::duration value)
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(value).count());
}

/// Накопитель общих счётчиков, таймеров и timeout одного запуска поиска.
struct SearchContext
{
  const GridEnvironment & environment;
  const SolverConfig & config;
  Clock::time_point start = Clock::now();
  Clock::duration generationTime{};
  Clock::duration validationTime{};
  SolverMetrics metrics;

  /// Проверяет только аварийный wall-clock timeout; ноль отключает его.
  bool timedOut() const { return config.timeoutMs > 0 && Clock::now() - start >= std::chrono::milliseconds(config.timeoutMs); }

  /// Получает кандидаты среды и учитывает время и количество генерации.
  std::vector<GridAction> candidates(const GridState & state, std::size_t instancePosition)
  {
    const Clock::time_point before = Clock::now();
    std::vector<GridAction> result = environment.enumerateCandidates(state, instancePosition);
    generationTime += Clock::now() - before;
    metrics.candidatesGenerated += result.size();
    return result;
  }

  /// Делегирует точную проверку среде и учитывает validation-метрики.
  bool valid(const GridState & state, const GridAction & action)
  {
    const Clock::time_point before = Clock::now();
    const bool result = environment.canApply(state, action);
    validationTime += Clock::now() - before;
    ++metrics.candidatesValidated;
    return result;
  }
};

/// Задаёт стабильный tie-break двух действий независимо от порядка в памяти.
bool placementLess(const GridPlacement & lhs, const GridPlacement & rhs)
{
  return std::tie(lhs.partId, lhs.instanceIndex, lhs.rotationDegrees, lhs.column, lhs.row) <
         std::tie(rhs.partId, rhs.instanceIndex, rhs.rotationDegrees, rhs.column, rhs.row);
}

/// Лексикографически сравнивает полные последовательности действий.
bool actionSequenceLess(const GridState & lhs, const GridState & rhs)
{
  return std::lexicographical_compare(lhs.placements.begin(), lhs.placements.end(), rhs.placements.begin(), rhs.placements.end(),
                                      placementLess);
}

/// Сравнивает состояния: полнота, объём partial, целевой остаток и стабильный tie-break.
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
  return actionSequenceLess(lhs, rhs);
}

/// Строит стабильную последовательность экземпляров согласно отдельной стратегии ordering.
std::vector<std::size_t> orderedInstances(const GridEnvironment & environment, PartOrdering ordering)
{
  std::vector<std::size_t> result(environment.instances().size());
  std::iota(result.begin(), result.end(), 0);
  if (ordering == PartOrdering::Input)
    return result;

  std::stable_sort(result.begin(), result.end(),
                   [&environment, ordering](std::size_t lhs, std::size_t rhs)
                   {
                     const GridPartInstance & left = environment.instances()[lhs];
                     const GridPartInstance & right = environment.instances()[rhs];
                     const GridPart & leftPart = environment.problem().parts[left.partIndex];
                     const GridPart & rightPart = environment.problem().parts[right.partIndex];
                     if (ordering == PartOrdering::AreaDescending && left.area != right.area)
                       return left.area > right.area;
                     if (left.maxDimension != right.maxDimension)
                       return left.maxDimension > right.maxDimension;
                     if (ordering == PartOrdering::MaxSideDescending && left.area != right.area)
                       return left.area > right.area;
                     if (leftPart.id != rightPart.id)
                       return leftPart.id < rightPart.id;
                     return left.instanceIndex < right.instanceIndex;
                   });
  return result;
}

/// Сравнивает два допустимых размещения по правилу valuable-remnant left-bottom.
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

/// Применяет к одному экземпляру отдельную политику выбора среди допустимых кандидатов.
bool placeOne(SearchContext & context, GridState & state, std::size_t instancePosition, PlacementPolicy policy)
{
  std::vector<GridAction> candidates = context.candidates(state, instancePosition);
  bool found = false;
  GridAction best;
  for (const GridAction & candidate : candidates)
  {
    if (context.timedOut())
      return false;
    if (!context.valid(state, candidate))
      continue;
    if (policy == PlacementPolicy::FirstFit)
    {
      context.environment.apply(state, candidate);
      ++context.metrics.expandedStates;
      return true;
    }
    if (!found || leftBottomLess(context.environment, state, instancePosition, candidate, best))
    {
      best = candidate;
      found = true;
    }
  }
  if (found)
  {
    context.environment.apply(state, best);
    ++context.metrics.expandedStates;
  }
  return found;
}

/// Последовательно применяет placement policy ко всем экземплярам, пропуская неразмещаемые.
GridState runOrdered(SearchContext & context, const std::vector<std::size_t> & order, PlacementPolicy policy)
{
  GridState state = context.environment.initialState();
  for (const std::size_t instancePosition : order)
  {
    if (context.timedOut())
      break;
    // Занятость только увеличивается, поэтому не поместившийся сейчас экземпляр
    // не станет допустимым позже. Остальные детали всё равно нужно проверить,
    // чтобы сохранить действительно лучший достижимый partial этого порядка.
    placeOne(context, state, instancePosition, policy);
  }
  return state;
}

/// Проверяет фиксированное число seeded-перестановок и сохраняет лучший partial или complete.
GridState runRandom(SearchContext & context, SolveStatus & status)
{
  GridState best = context.environment.initialState();
  std::vector<std::size_t> order = orderedInstances(context.environment, PartOrdering::Input);
  std::mt19937_64 random(context.config.seed);
  for (std::size_t iteration = 0; iteration < context.config.randomIterations; ++iteration)
  {
    if (context.timedOut())
    {
      status = SolveStatus::TimedOut;
      return best;
    }
    // Один генератор последовательно создаёт воспроизводимую серию перестановок;
    // временные измерения не влияют на выбор действий.
    std::shuffle(order.begin(), order.end(), random);
    GridState candidate = runOrdered(context, order, PlacementPolicy::LeftBottom);
    if (betterState(context.environment, candidate, best))
      best = std::move(candidate);
  }
  status = best.placements.size() == context.environment.instances().size() ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
  return best;
}

/// Расширяет общий action space слоями, оставляя beamWidth лучших состояний.
GridState runBeam(SearchContext & context, SolveStatus & status)
{
  GridState best = context.environment.initialState();
  std::vector<GridState> beam{best};

  while (!beam.empty() && best.placements.size() < context.environment.instances().size())
  {
    std::vector<GridState> children;
    bool stoppedByBudget = false;
    for (const GridState & state : beam)
    {
      for (std::size_t instancePosition = 0; instancePosition < context.environment.instances().size(); ++instancePosition)
      {
        if (state.placedInstances[instancePosition])
          continue;
        const GridPartInstance & instance = context.environment.instances()[instancePosition];
        // Экземпляры одного part взаимозаменяемы. Расширяем только минимальный
        // ещё не размещённый instanceIndex, чтобы не создавать перестановочные дубли.
        bool earlierEquivalentUnplaced = false;
        for (std::size_t earlier = 0; earlier < instancePosition; ++earlier)
        {
          const GridPartInstance & previous = context.environment.instances()[earlier];
          if (previous.partIndex == instance.partIndex && !state.placedInstances[earlier])
          {
            earlierEquivalentUnplaced = true;
            break;
          }
        }
        if (earlierEquivalentUnplaced)
          continue;

        const std::vector<GridAction> candidates = context.candidates(state, instancePosition);
        for (const GridAction & action : candidates)
        {
          if (context.timedOut())
          {
            status = SolveStatus::TimedOut;
            return best;
          }
          if (!context.valid(state, action))
            continue;
          if (context.metrics.expandedStates >= context.config.maxExpandedStates)
          {
            stoppedByBudget = true;
            break;
          }
          GridState child = state;
          context.environment.apply(child, action);
          ++context.metrics.expandedStates;
          if (betterState(context.environment, child, best))
            best = child;
          children.push_back(std::move(child));
        }
        if (stoppedByBudget)
          break;
      }
      if (stoppedByBudget)
        break;
    }

    if (stoppedByBudget)
    {
      status =
        best.placements.size() == context.environment.instances().size() ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
      return best;
    }
    if (children.empty())
      break;
    // Стабильная сортировка вместе с action tie-break делает усечение beam
    // одинаковым на разных запусках и стандартных библиотеках.
    std::stable_sort(children.begin(), children.end(), [&context](const GridState & lhs, const GridState & rhs)
                     { return betterState(context.environment, lhs, rhs); });
    if (children.size() > context.config.beamWidth)
      children.resize(context.config.beamWidth);
    beam = std::move(children);
  }
  status = best.placements.size() == context.environment.instances().size() ? SolveStatus::Solved : SolveStatus::NoSolutionFound;
  return best;
}

/// Формирует сериализуемые сведения об алгоритме, бюджете и ревизии сборки.
SolverMetadata metadata(const SolverConfig & config)
{
  return {toString(config.solver),    AIPACKAGING_PROJECT_VERSION,
          AIPACKAGING_BUILD_REVISION, config.seed,
          config.randomIterations,    config.beamWidth,
          config.maxExpandedStates,   config.timeoutMs};
}
} // namespace

/// Преобразует статус enum в закреплённое schema v1 имя.
std::string toString(SolveStatus status)
{
  switch (status)
  {
    case SolveStatus::Solved:
      return "solved";
    case SolveStatus::NoSolutionFound:
      return "no_solution_found";
    case SolveStatus::BudgetExhausted:
      return "budget_exhausted";
    case SolveStatus::TimedOut:
      return "timed_out";
    case SolveStatus::InvalidProblem:
      return "invalid_problem";
  }
  return "invalid_problem";
}

/// Преобразует вид решателя в закреплённое CLI/schema v1 имя.
std::string toString(SolverKind solver)
{
  switch (solver)
  {
    case SolverKind::InputFirstFit:
      return "input-first-fit";
    case SolverKind::AreaLeftBottom:
      return "area-left-bottom";
    case SolverKind::MaxSideLeftBottom:
      return "max-side-left-bottom";
    case SolverKind::RandomLeftBottom:
      return "random-left-bottom";
    case SolverKind::Beam:
      return "beam";
  }
  return "area-left-bottom";
}

/// Ищет статус по всем допустимым строковым значениям schema v1.
bool parseSolveStatus(const std::string & value, SolveStatus & status)
{
  constexpr SolveStatus VALUES[] = {SolveStatus::Solved, SolveStatus::NoSolutionFound, SolveStatus::BudgetExhausted,
                                    SolveStatus::TimedOut, SolveStatus::InvalidProblem};
  for (const SolveStatus candidate : VALUES)
  {
    if (toString(candidate) == value)
    {
      status = candidate;
      return true;
    }
  }
  return false;
}

/// Ищет алгоритм по всем допустимым строковым значениям CLI.
bool parseSolverKind(const std::string & value, SolverKind & solver)
{
  constexpr SolverKind VALUES[] = {SolverKind::InputFirstFit, SolverKind::AreaLeftBottom, SolverKind::MaxSideLeftBottom,
                                   SolverKind::RandomLeftBottom, SolverKind::Beam};
  for (const SolverKind candidate : VALUES)
  {
    if (toString(candidate) == value)
    {
      solver = candidate;
      return true;
    }
  }
  return false;
}

/// Проверяет вход, запускает выбранный алгоритм и собирает независимые метрики результата.
GridSolution solveGridProblem(const GridProblem & problem, const SolverConfig & config)
{
  GridSolution solution;
  solution.problemId = problem.problemId;
  solution.solver = metadata(config);

  if ((config.solver == SolverKind::RandomLeftBottom && config.randomIterations == 0) ||
      (config.solver == SolverKind::Beam && (config.beamWidth == 0 || config.maxExpandedStates == 0)))
  {
    solution.errorMessage = "solver budgets must be positive";
    return solution;
  }

  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  if (!environment)
  {
    solution.errorMessage = error;
    return solution;
  }

  SearchContext context{*environment, config};
  SolveStatus status = SolveStatus::NoSolutionFound;
  GridState state;
  switch (config.solver)
  {
    case SolverKind::InputFirstFit:
      state = runOrdered(context, orderedInstances(*environment, PartOrdering::Input), PlacementPolicy::FirstFit);
      status = context.timedOut()                                         ? SolveStatus::TimedOut
             : state.placements.size() == environment->instances().size() ? SolveStatus::Solved
                                                                          : SolveStatus::NoSolutionFound;
      break;
    case SolverKind::AreaLeftBottom:
      state = runOrdered(context, orderedInstances(*environment, PartOrdering::AreaDescending), PlacementPolicy::LeftBottom);
      status = context.timedOut()                                         ? SolveStatus::TimedOut
             : state.placements.size() == environment->instances().size() ? SolveStatus::Solved
                                                                          : SolveStatus::NoSolutionFound;
      break;
    case SolverKind::MaxSideLeftBottom:
      state = runOrdered(context, orderedInstances(*environment, PartOrdering::MaxSideDescending), PlacementPolicy::LeftBottom);
      status = context.timedOut()                                         ? SolveStatus::TimedOut
             : state.placements.size() == environment->instances().size() ? SolveStatus::Solved
                                                                          : SolveStatus::NoSolutionFound;
      break;
    case SolverKind::RandomLeftBottom:
      state = runRandom(context, status);
      break;
    case SolverKind::Beam:
      state = runBeam(context, status);
      break;
  }

  // Search time хранится отдельно от явно измеренных генерации и валидации.
  // Небольшая погрешность округления вниз безопасно ограничивается нулём.
  const Clock::duration total = Clock::now() - context.start;
  context.metrics.candidateGenerationTimeUs = microseconds(context.generationTime);
  context.metrics.validationTimeUs = microseconds(context.validationTime);
  context.metrics.totalTimeUs = microseconds(total);
  const std::uint64_t measured = context.metrics.candidateGenerationTimeUs + context.metrics.validationTimeUs;
  context.metrics.searchTimeUs = context.metrics.totalTimeUs > measured ? context.metrics.totalTimeUs - measured : 0;

  solution.status = status;
  solution.objective = environment->evaluate(state);
  solution.placements = std::move(state.placements);
  solution.metrics = context.metrics;
  if (!solution.complete())
    solution.errorMessage = toString(status);
  return solution;
}
} // namespace aipackaging::solver
