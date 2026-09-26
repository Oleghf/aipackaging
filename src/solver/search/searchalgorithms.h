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

/// Возвращает стабильный входной порядок экземпляров либо порядок по площади или максимальной стороне.
template<class Adapter>
std::vector<std::size_t> orderedInstances(const Adapter & adapter, PartOrdering ordering)
{
  std::vector<std::size_t> result(adapter.instanceCount());
  std::iota(result.begin(), result.end(), 0);
  if (ordering == PartOrdering::Input)
    return result;

  // Геометрические величины предоставляет адаптер, а общий механизм выполнения фиксирует
  // одинаковую последовательность разрешения равенства для обеих предметных областей.
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

/// Последовательно обрабатывает порядок и возвращает частичное или полное состояние.
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

/// Выполняет перестановки с заданным начальным значением и сохраняет лучшее найденное состояние.
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
    // выделения механизма выполнения; измерения и обратные вызовы не влияют на выбор.
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

/// Классифицирует причину досрочного завершения расширения лучевого слоя.
enum class BeamExpansionResult : std::uint8_t
{
  Continue,
  SearchStopped,
  BudgetExhausted
};

/// Сообщает, что меньший неразмещённый индекс уже представляет тот же тип детали.
template<class Adapter>
bool hasEarlierEquivalentUnplaced(const Adapter & adapter, const typename Adapter::State & state, std::size_t instance)
{
  for (std::size_t earlier = 0; earlier < instance; ++earlier)
    if (!adapter.isPlaced(state, earlier) && adapter.samePart(earlier, instance))
      return true;
  return false;
}

/// Уточняет публичный статус только для наблюдённого ограничения времени.
inline void recordSearchStopStatus(const SearchRuntime & runtime, SolveStatus & status)
{
  if (runtime.stopReason() == SearchStopReason::TimedOut)
    status = SolveStatus::TimedOut;
}

/// Проверяет бюджет в предметно-зависимой границе до либо после точной валидации.
template<class Adapter>
bool beamBudgetReached(const SearchRuntime & runtime, const Adapter & adapter, bool beforeValidation)
{
  return adapter.budgetBeforeValidation() == beforeValidation && runtime.expandedStates() >= runtime.config().maxExpandedStates;
}

/// Проверяет и применяет одно действие, сохраняя прежние границы бюджета и остановки.
template<class Adapter>
BeamExpansionResult expandBeamAction(SearchRuntime & runtime, const Adapter & adapter, const typename Adapter::State & state,
                                     const typename Adapter::Action & action, typename Adapter::State & best,
                                     std::vector<typename Adapter::State> & children, SolveStatus & status)
{
  if (runtime.pollStop())
  {
    recordSearchStopStatus(runtime, status);
    return BeamExpansionResult::SearchStopped;
  }
  if (beamBudgetReached(runtime, adapter, true))
    return BeamExpansionResult::BudgetExhausted;

  const auto validationStarted = runtime.now();
  const bool valid = adapter.valid(state, action);
  runtime.recordValidation(validationStarted);
  if (!valid)
    return BeamExpansionResult::Continue;
  if (beamBudgetReached(runtime, adapter, false))
    return BeamExpansionResult::BudgetExhausted;

  typename Adapter::State child = state;
  adapter.apply(child, action);
  runtime.recordExpansion();
  if (adapter.better(child, best))
    best = child;
  children.push_back(std::move(child));
  runtime.report(SearchProgressStage::ExpandedStates, runtime.expandedStates(), runtime.config().maxExpandedStates,
                 adapter.placedCount(best), adapter.instanceCount());
  return BeamExpansionResult::Continue;
}

/// Расширяет одно состояние всеми допустимыми действиями в прежнем порядке экземпляров.
template<class Adapter>
BeamExpansionResult expandBeamState(SearchRuntime & runtime, const Adapter & adapter, const typename Adapter::State & state,
                                    typename Adapter::State & best, std::vector<typename Adapter::State> & children,
                                    SolveStatus & status)
{
  for (std::size_t instance = 0; instance < adapter.instanceCount(); ++instance)
  {
    if (adapter.isPlaced(state, instance) || hasEarlierEquivalentUnplaced(adapter, state, instance))
      continue;

    const auto generationStarted = runtime.now();
    const auto candidates = adapter.candidates(state, instance);
    runtime.recordCandidateGeneration(generationStarted, candidates.size());
    for (const typename Adapter::Action & action : candidates)
    {
      const BeamExpansionResult result = expandBeamAction(runtime, adapter, state, action, best, children, status);
      if (result != BeamExpansionResult::Continue)
        return result;
    }
  }
  return BeamExpansionResult::Continue;
}

/// Формирует полный следующий слой либо возвращает первую причину остановки.
template<class Adapter>
BeamExpansionResult expandBeamLayer(SearchRuntime & runtime, const Adapter & adapter,
                                    const std::vector<typename Adapter::State> & beam, typename Adapter::State & best,
                                    std::vector<typename Adapter::State> & children, SolveStatus & status)
{
  for (const typename Adapter::State & state : beam)
  {
    const BeamExpansionResult result = expandBeamState(runtime, adapter, state, best, children, status);
    if (result != BeamExpansionResult::Continue)
      return result;
  }
  return BeamExpansionResult::Continue;
}

/// Стабильно оставляет в слое не более заданной ширины лучших состояний.
template<class Adapter>
void truncateBeam(const Adapter & adapter, std::vector<typename Adapter::State> & children, std::size_t beamWidth)
{
  std::stable_sort(children.begin(), children.end(),
                   [&adapter](const auto & lhs, const auto & rhs) { return adapter.better(lhs, rhs); });
  if (children.size() > beamWidth)
    children.resize(beamWidth);
}

/// Определяет итоговый статус после естественного завершения лучевого поиска.
template<class Adapter>
SolveStatus finalBeamStatus(const SearchRuntime & runtime, const Adapter & adapter, const typename Adapter::State & best)
{
  if (adapter.complete(best))
    return SolveStatus::Solved;
  return runtime.expandedStates() >= runtime.config().maxExpandedStates ? SolveStatus::BudgetExhausted
                                                                        : SolveStatus::NoSolutionFound;
}

/// Расширяет общее пространство действий слоями и сохраняет до beamWidth лучших состояний.
template<class Adapter>
Adapter::State runBeam(SearchRuntime & runtime, const Adapter & adapter, SolveStatus & status)
{
  typename Adapter::State best = adapter.initialState();
  std::vector<typename Adapter::State> beam{best};

  while (!beam.empty() && !adapter.complete(best))
  {
    if (runtime.pollStop())
    {
      recordSearchStopStatus(runtime, status);
      return best;
    }
    std::vector<typename Adapter::State> children;
    const BeamExpansionResult expansion = expandBeamLayer(runtime, adapter, beam, best, children, status);
    if (expansion == BeamExpansionResult::SearchStopped)
      return best;
    if (expansion == BeamExpansionResult::BudgetExhausted)
    {
      status = adapter.complete(best) ? SolveStatus::Solved : SolveStatus::BudgetExhausted;
      return best;
    }
    if (children.empty())
      break;
    // Стабильное предметное разрешение равенства сохраняет одинаковое усечение.
    truncateBeam(adapter, children, runtime.config().beamWidth);
    beam = std::move(children);
  }

  status = finalBeamStatus(runtime, adapter, best);
  return best;
}
} // namespace aipackaging::solver::detail

#endif
