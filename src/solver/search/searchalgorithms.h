#ifndef AIPACKAGING_SEARCH_ALGORITHMS_H
#define AIPACKAGING_SEARCH_ALGORITHMS_H

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

#include "searchruntime.h"

namespace aipackaging::solver::detail
{
/// Стратегия построения воспроизводимого порядка обязательных экземпляров.
enum class PartOrdering : std::uint8_t
{
  Input,
  AreaDescending,
  MaxSideDescending
};

/// Стратегия выбора размещения уже выбранного экземпляра.
enum class PlacementPolicy : std::uint8_t
{
  FirstFit,
  LeftBottom
};

/// Возвращает стабильный input/area/max-side порядок экземпляров адаптера.
template<class Adapter>
std::vector<std::size_t> orderedInstances(const Adapter & adapter, PartOrdering ordering)
{
  std::vector<std::size_t> result(adapter.instanceCount());
  std::iota(result.begin(), result.end(), 0);
  if (ordering == PartOrdering::Input)
    return result;

  // Геометрические величины предоставляет адаптер, а общий runtime фиксирует
  // одинаковую последовательность tie-break для обеих предметных областей.
  std::stable_sort(result.begin(), result.end(),
                   [&adapter, ordering](std::size_t lhs, std::size_t rhs)
                   {
                     if (ordering == PartOrdering::AreaDescending && adapter.instanceArea(lhs) != adapter.instanceArea(rhs))
                       return adapter.instanceArea(lhs) > adapter.instanceArea(rhs);
                     if (adapter.instanceMaxDimension(lhs) != adapter.instanceMaxDimension(rhs))
                       return adapter.instanceMaxDimension(lhs) > adapter.instanceMaxDimension(rhs);
                     if (ordering == PartOrdering::MaxSideDescending && adapter.instanceArea(lhs) != adapter.instanceArea(rhs))
                       return adapter.instanceArea(lhs) > adapter.instanceArea(rhs);
                     if (adapter.instancePartId(lhs) != adapter.instancePartId(rhs))
                       return adapter.instancePartId(lhs) < adapter.instancePartId(rhs);
                     return adapter.instanceIndex(lhs) < adapter.instanceIndex(rhs);
                   });
  return result;
}

/// Последовательно обрабатывает заданный порядок и возвращает достигнутый partial или complete state.
template<class Adapter>
Adapter::State runOrdered(SearchRuntime & runtime, const Adapter & adapter, const std::vector<std::size_t> & order,
                          PlacementPolicy policy, bool reportInstances = true)
{
  typename Adapter::State state = adapter.initialState();
  for (std::size_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
  {
    if (runtime.pollStop())
      break;
    adapter.placeOrdered(runtime, state, order[orderIndex], policy);
    if (reportInstances)
      runtime.report(SearchProgressStage::Instances, orderIndex + 1, order.size(), adapter.placedCount(state),
                     adapter.instanceCount());
  }
  return state;
}

/// Выполняет seeded-перестановки и сохраняет лучший найденный state.
template<class Adapter>
Adapter::State runRandom(SearchRuntime & runtime, const Adapter & adapter, SolveStatus & status)
{
  typename Adapter::State best = adapter.initialState();
  std::vector<std::size_t> order = orderedInstances(adapter, PartOrdering::Input);
  std::mt19937_64 random(runtime.config().seed);
  for (std::size_t iteration = 0; iteration < runtime.config().randomIterations; ++iteration)
  {
    if (runtime.pollStop())
    {
      if (runtime.stopReason() == SearchStopReason::TimedOut)
        status = SolveStatus::TimedOut;
      return best;
    }
    // Один PRNG последовательно создаёт ту же Fisher-Yates серию, что и до
    // выделения runtime; измерения и callback не участвуют в выборе действий.
    std::shuffle(order.begin(), order.end(), random);
    typename Adapter::State candidate = runOrdered(runtime, adapter, order, PlacementPolicy::LeftBottom, false);
    if (adapter.better(candidate, best))
      best = std::move(candidate);
    runtime.report(SearchProgressStage::RandomIterations, iteration + 1, runtime.config().randomIterations,
                   adapter.placedCount(best), adapter.instanceCount());
    if (runtime.stopReason() != SearchStopReason::None)
    {
      if (runtime.stopReason() == SearchStopReason::TimedOut)
        status = SolveStatus::TimedOut;
      return best;
    }
  }
  status = adapter.complete(best) ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
  return best;
}

/// Расширяет совместный action space слоями и сохраняет не более beamWidth лучших состояний.
template<class Adapter>
Adapter::State runBeam(SearchRuntime & runtime, const Adapter & adapter, SolveStatus & status)
{
  typename Adapter::State best = adapter.initialState();
  std::vector<typename Adapter::State> beam{best};

  while (!beam.empty() && !adapter.complete(best))
  {
    if (runtime.pollStop())
    {
      if (runtime.stopReason() == SearchStopReason::TimedOut)
        status = SolveStatus::TimedOut;
      return best;
    }
    std::vector<typename Adapter::State> children;
    bool stoppedByBudget = false;
    for (const typename Adapter::State & state : beam)
    {
      for (std::size_t instance = 0; instance < adapter.instanceCount(); ++instance)
      {
        if (adapter.isPlaced(state, instance))
          continue;

        // Экземпляры одного типа взаимозаменяемы. Минимальный ещё не
        // размещённый индекс устраняет перестановочные дубли в beam.
        bool earlierEquivalentUnplaced = false;
        for (std::size_t earlier = 0; earlier < instance; ++earlier)
          if (!adapter.isPlaced(state, earlier) && adapter.samePart(earlier, instance))
          {
            earlierEquivalentUnplaced = true;
            break;
          }
        if (earlierEquivalentUnplaced)
          continue;

        const auto generationStarted = runtime.now();
        const auto candidates = adapter.candidates(state, instance);
        runtime.recordCandidateGeneration(generationStarted, candidates.size());
        for (const typename Adapter::Action & action : candidates)
        {
          if (runtime.pollStop())
          {
            if (runtime.stopReason() == SearchStopReason::TimedOut)
              status = SolveStatus::TimedOut;
            return best;
          }
          if (adapter.budgetBeforeValidation() && runtime.expandedStates() >= runtime.config().maxExpandedStates)
          {
            stoppedByBudget = true;
            break;
          }
          const auto validationStarted = runtime.now();
          const bool valid = adapter.valid(state, action);
          runtime.recordValidation(validationStarted);
          if (!valid)
            continue;
          if (!adapter.budgetBeforeValidation() && runtime.expandedStates() >= runtime.config().maxExpandedStates)
          {
            stoppedByBudget = true;
            break;
          }
          typename Adapter::State child = state;
          adapter.apply(child, action);
          runtime.recordExpansion();
          if (adapter.better(child, best))
            best = child;
          children.push_back(std::move(child));
          runtime.report(SearchProgressStage::ExpandedStates, runtime.expandedStates(), runtime.config().maxExpandedStates,
                         adapter.placedCount(best), adapter.instanceCount());
        }
        if (stoppedByBudget)
          break;
      }
      if (stoppedByBudget)
        break;
    }

    if (stoppedByBudget)
    {
      status = adapter.complete(best) ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
      return best;
    }
    if (children.empty())
      break;
    // Стабильная сортировка вместе с domain tie-break сохраняет одинаковое
    // усечение beam на разных запусках и реализациях стандартной библиотеки.
    std::stable_sort(children.begin(), children.end(),
                     [&adapter](const auto & lhs, const auto & rhs) { return adapter.better(lhs, rhs); });
    if (children.size() > runtime.config().beamWidth)
      children.resize(runtime.config().beamWidth);
    beam = std::move(children);
  }

  status = adapter.complete(best)
           ? SolveStatus::Solved
           : (runtime.expandedStates() >= runtime.config().maxExpandedStates ? SolveStatus::BudgetExhausted
                                                                             : SolveStatus::NoSolutionFound);
  return best;
}
} // namespace aipackaging::solver::detail

#endif
