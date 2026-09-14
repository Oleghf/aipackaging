#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <aipackaging/nesting/polygon_learning.h>

namespace aipackaging::solver
{
namespace
{
constexpr int RASTER_SIZE = 128;
constexpr int SHAPE_RASTER_SIZE = 32;
constexpr double REWARD_RADIX = 1024.0;

/// Проверяет попадание точки внутрь либо на границу кольца лучевым методом.
bool containsPoint(const PolygonRing64 & ring, const PolygonPoint64 & point)
{
  bool inside = false;
  for (std::size_t index = 0, previous = ring.size() - 1; index < ring.size(); previous = index++)
  {
    const auto & a = ring[previous];
    const auto & b = ring[index];
    const long double cross =
      static_cast<long double>(b.x - a.x) * (point.y - a.y) - static_cast<long double>(b.y - a.y) * (point.x - a.x);
    if (cross == 0 && point.x >= std::min(a.x, b.x) && point.x <= std::max(a.x, b.x) && point.y >= std::min(a.y, b.y) &&
        point.y <= std::max(a.y, b.y))
      return true;
    if ((a.y > point.y) != (b.y > point.y))
    {
      const long double x = static_cast<long double>(b.x - a.x) * (point.y - a.y) / static_cast<long double>(b.y - a.y) + a.x;
      if (point.x < x)
        inside = !inside;
    }
  }
  return inside;
}

/// Заполняет каналы занятости и зазора выборками в центрах клеток текущей раскладки.
void rasterState(const PolygonEnvironment & environment, const PolygonState & state, std::vector<float> & occupied,
                 std::vector<float> & clearance)
{
  occupied.assign(RASTER_SIZE * RASTER_SIZE, 0.0F);
  clearance.assign(RASTER_SIZE * RASTER_SIZE, 0.0F);
  const std::int64_t usableWidth = environment.sheetWidth() - 2 * environment.sheetMargin();
  const std::int64_t usableHeight = environment.sheetHeight() - 2 * environment.sheetMargin();
  const long double clearanceSquared = static_cast<long double>(environment.partSpacing()) * environment.partSpacing();
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < RASTER_SIZE; ++column)
    {
      const PolygonPoint64 point{environment.sheetMargin() + (2LL * column + 1) * usableWidth / (2 * RASTER_SIZE),
                                 environment.sheetMargin() + (2LL * row + 1) * usableHeight / (2 * RASTER_SIZE)};
      const std::size_t offset = static_cast<std::size_t>(row * RASTER_SIZE + column);
      for (const PolygonPlacement & placement : state.placements)
      {
        const PolygonRing64 ring = environment.placedOuter(placement);
        if (containsPoint(ring, point))
        {
          occupied[offset] = 1.0F;
          clearance[offset] = 1.0F;
          break;
        }
        if (environment.partSpacing() > 0)
        {
          long double best = std::numeric_limits<long double>::max();
          for (std::size_t index = 0; index < ring.size(); ++index)
          {
            const auto & a = ring[index];
            const auto & b = ring[(index + 1) % ring.size()];
            const long double dx = b.x - a.x;
            const long double dy = b.y - a.y;
            const long double length = dx * dx + dy * dy;
            const long double t =
              length == 0 ? 0 : std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0L, 1.0L);
            const long double px = point.x - (a.x + t * dx);
            const long double py = point.y - (a.y + t * dy);
            best = std::min(best, px * px + py * py);
          }
          if (best < clearanceSquared)
            clearance[offset] = 1.0F;
        }
      }
    }
}

/// Возвращает нормализованное скалярное значение хода выполнения до единицы.
double progress(const PolygonObjectiveComponents & objective)
{
  return objective.totalParts == 0 ? 0.0 : static_cast<double>(objective.placedParts) / objective.totalParts;
}

/// Формирует нормализованные компоненты потенциала вознаграждения v2.
std::array<double, 5> rewardComponents(const PolygonEnvironment & environment, const PolygonState & state)
{
  const PolygonObjectiveComponents objective = environment.evaluate(state);
  if (objective.placedParts == 0 || objective.totalParts == 0)
    return {};
  const double usableWidth = std::max<std::int64_t>(1, environment.sheetWidth() - 2 * environment.sheetMargin());
  const double usableHeight = std::max<std::int64_t>(1, environment.sheetHeight() - 2 * environment.sheetMargin());
  const double usableArea = usableWidth * usableHeight;
  return {static_cast<double>(objective.placedParts),
          objective.placedArea / static_cast<double>(std::max<std::uint64_t>(1, objective.totalPartArea)),
          std::clamp(objective.primaryRemnantWidth / usableWidth, 0.0, 1.0),
          std::clamp(objective.largestExtraRectangleArea / usableArea, 0.0, 1.0),
          std::clamp(1.0 - objective.fragmentationPenalty / usableArea, 0.0, 1.0)};
}

/// Сворачивает компоненты v2 в ограниченный потенциал со строгим приоритетом числа деталей.
double rewardPotential(const PolygonEnvironment & environment, const PolygonState & state, int rewardVersion,
                       std::array<double, 5> & components)
{
  const PolygonObjectiveComponents objective = environment.evaluate(state);
  if (rewardVersion == 1)
  {
    components = {progress(objective), 0.0, 0.0, 0.0, 0.0};
    return components[0];
  }
  components = rewardComponents(environment, state);
  if (objective.totalParts == 0 || objective.placedParts == 0)
    return 0.0;
  const double secondary = components[1] + components[2] / REWARD_RADIX + components[3] / (REWARD_RADIX * REWARD_RADIX) +
                           components[4] / (REWARD_RADIX * REWARD_RADIX * REWARD_RADIX);
  return (components[0] + 0.5 * secondary) / objective.totalParts;
}
} // namespace

/// Создаёт точную среду и преобразует ошибки геометрии в публичную диагностику.
std::unique_ptr<PolygonLearningEnvironment> PolygonLearningEnvironment::Create(const PolygonProblem & problem,
                                                                               std::string & error)
{
  return Create(problem, {}, error);
}

/// Проверяет версию вознаграждения, создаёт точную среду и преобразует ошибки геометрии в диагностику.
std::unique_ptr<PolygonLearningEnvironment>
PolygonLearningEnvironment::Create(const PolygonProblem & problem, const PolygonLearningConfig & config, std::string & error)
{
  if (config.rewardVersion != 1 && config.rewardVersion != 2)
  {
    error = "поддерживаются только версии полигонального вознаграждения 1 и 2";
    return nullptr;
  }
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
    return nullptr;
  try
  {
    return std::unique_ptr<PolygonLearningEnvironment>(new PolygonLearningEnvironment(std::move(environment), config));
  }
  catch (const std::length_error & exception)
  {
    error = exception.what();
    return nullptr;
  }
}

/// Создаёт начальное состояние и первый динамический каталог.
PolygonLearningEnvironment::PolygonLearningEnvironment(std::unique_ptr<PolygonEnvironment> environment,
                                                       PolygonLearningConfig config)
  : environment_(std::move(environment))
  , state_(environment_->initialState())
  , config_(config)
{
  rebuildActions();
}

/// Перечисляет допустимые кандидаты всех ещё не размещённых экземпляров.
void PolygonLearningEnvironment::rebuildActions()
{
  actions_.clear();
  for (std::size_t instance = 0; instance < environment_->instances().size(); ++instance)
  {
    if (state_.placedInstances[instance])
      continue;
    std::vector<PolygonAction> candidates = environment_->enumerateCandidates(state_, instance);
    actions_.insert(actions_.end(), candidates.begin(), candidates.end());
    if (actions_.size() > 250000)
      throw std::length_error("каталог полигональных действий превышает 250000 записей");
  }
}

/// Восстанавливает пустое изменяемое состояние и пересчитывает каталог.
PolygonObservation PolygonLearningEnvironment::reset()
{
  state_ = environment_->initialState();
  rebuildActions();
  return observation();
}

/// Растеризует состояние и формирует признаки экземпляров и целевой функции.
PolygonObservation PolygonLearningEnvironment::observation() const
{
  const PolygonDynamicObservation dynamic = dynamicObservation();
  const PolygonStaticObservation fixed = staticObservation();
  PolygonObservation result;
  result.occupied = dynamic.occupied;
  result.clearance = dynamic.clearance;
  result.remaining = dynamic.remaining;
  result.partFeatures = fixed.partFeatures;
  result.objective = dynamic.objective;
  return result;
}

/// Растеризует все разрешённые ориентации один раз и добавляет неизменные признаки экземпляров.
PolygonStaticObservation PolygonLearningEnvironment::staticObservation() const
{
  PolygonStaticObservation result;
  const std::size_t instanceCount = environment_->instances().size();
  result.partMasks.assign(instanceCount * 4 * SHAPE_RASTER_SIZE * SHAPE_RASTER_SIZE, 0);
  result.orientationMask.assign(instanceCount * 4, 0);
  result.partFeatures.reserve(instanceCount * 7);
  result.instancePartIds.reserve(instanceCount);
  result.instanceIndices.reserve(instanceCount);
  const double sheetArea = static_cast<double>(environment_->sheetWidth()) * environment_->sheetHeight();
  for (std::size_t index = 0; index < instanceCount; ++index)
  {
    const auto & instance = environment_->instances()[index];
    const auto & orientations = environment_->orientations(instance.partIndex);
    result.instancePartIds.push_back(environment_->problem().parts[instance.partIndex].id);
    result.instanceIndices.push_back(instance.instanceIndex);
    result.partFeatures.insert(
      result.partFeatures.end(),
      {static_cast<float>(instance.area / sheetArea),
       static_cast<float>(orientations.front().width / static_cast<double>(environment_->sheetWidth())),
       static_cast<float>(orientations.front().height / static_cast<double>(environment_->sheetHeight())),
       static_cast<float>(orientations.size() / 4.0),
       static_cast<float>(instance.partIndex / std::max<double>(1.0, environment_->problem().parts.size() - 1.0)),
       static_cast<float>(instance.instanceIndex /
                          std::max<double>(1.0, environment_->problem().parts[instance.partIndex].quantity - 1.0)),
       static_cast<float>(environment_->problem().parts[instance.partIndex].quantity /
                          static_cast<double>(environment_->instances().size()))});
    for (int slot = 0; slot < 4; ++slot)
    {
      const PolygonOrientation * orientation = environment_->findOrientation(instance.partIndex, slot * 90);
      if (!orientation)
        continue;
      result.orientationMask[index * 4 + static_cast<std::size_t>(slot)] = 1;
      for (int row = 0; row < SHAPE_RASTER_SIZE; ++row)
        for (int column = 0; column < SHAPE_RASTER_SIZE; ++column)
        {
          const PolygonPoint64 point{(2LL * column + 1) * orientation->width / (2 * SHAPE_RASTER_SIZE),
                                     (2LL * row + 1) * orientation->height / (2 * SHAPE_RASTER_SIZE)};
          if (containsPoint(orientation->outer, point))
          {
            const std::size_t offset =
              ((index * 4 + static_cast<std::size_t>(slot)) * SHAPE_RASTER_SIZE + row) * SHAPE_RASTER_SIZE + column;
            result.partMasks[offset] = 1;
          }
        }
    }
  }
  return result;
}

/// Растеризует текущее состояние и выводит допустимые пары из динамического каталога.
PolygonDynamicObservation PolygonLearningEnvironment::dynamicObservation() const
{
  PolygonDynamicObservation result;
  rasterState(*environment_, state_, result.occupied, result.clearance);
  result.remaining.reserve(environment_->instances().size());
  result.pairMask.assign(environment_->instances().size() * 4, 0);
  for (std::size_t index = 0; index < environment_->instances().size(); ++index)
    result.remaining.push_back(state_.placedInstances[index] ? 0 : 1);
  for (const PolygonAction & action : actions_)
  {
    const std::size_t instance = environment_->findInstance(action.partId, action.instanceIndex);
    if (instance < environment_->instances().size())
      result.pairMask[instance * 4 + static_cast<std::size_t>(action.rotationDegrees / 90)] = 1;
  }
  const PolygonObjectiveComponents objective = environment_->evaluate(state_);
  const double usableWidth = environment_->sheetWidth() - 2.0 * environment_->sheetMargin();
  const double sheetArea = static_cast<double>(environment_->sheetWidth()) * environment_->sheetHeight();
  result.objective = {static_cast<float>(progress(objective)),
                      static_cast<float>(objective.placedArea / std::max(1.0, sheetArea)),
                      static_cast<float>(objective.usedLength / std::max(1.0, usableWidth)),
                      static_cast<float>(objective.primaryRemnantWidth / std::max(1.0, usableWidth)),
                      static_cast<float>(objective.largestExtraRectangleArea / std::max(1.0, sheetArea)),
                      static_cast<float>(objective.fragmentationPenalty / std::max(1.0, sheetArea)),
                      static_cast<float>(objective.materialUtilization)};
  return result;
}

/// Восстанавливает исходное состояние и не пересоздаёт статические растры деталей.
PolygonDynamicObservation PolygonLearningEnvironment::resetCompact()
{
  state_ = environment_->initialState();
  rebuildActions();
  return dynamicObservation();
}

/// Собирает условное растровое представление и только кандидаты выбранной иерархической пары.
PolygonPlacementObservation PolygonLearningEnvironment::placementObservation(std::size_t instancePosition,
                                                                             int rotationDegrees) const
{
  if (instancePosition >= environment_->instances().size())
    throw std::out_of_range("индекс экземпляра полигональной детали находится вне каталога");
  const auto & instance = environment_->instances()[instancePosition];
  const PolygonOrientation * orientation = environment_->findOrientation(instance.partIndex, rotationDegrees);
  if (!orientation)
    throw std::invalid_argument("поворот полигональной детали недоступен для экземпляра");
  PolygonPlacementObservation result;
  std::vector<float> occupied;
  std::vector<float> clearance;
  rasterState(*environment_, state_, occupied, clearance);
  result.channels.resize(4 * RASTER_SIZE * RASTER_SIZE, 0.0F);
  std::copy(occupied.begin(), occupied.end(), result.channels.begin());
  std::copy(clearance.begin(), clearance.end(), result.channels.begin() + RASTER_SIZE * RASTER_SIZE);
  const std::int64_t usableWidth = environment_->sheetWidth() - 2 * environment_->sheetMargin();
  const std::int64_t usableHeight = environment_->sheetHeight() - 2 * environment_->sheetMargin();

  // Третий канал содержит ориентацию в нормализованной локальной системе.
  for (int row = 0; row < RASTER_SIZE; ++row)
    for (int column = 0; column < RASTER_SIZE; ++column)
    {
      const PolygonPoint64 local{(2LL * column + 1) * orientation->width / (2 * RASTER_SIZE),
                                 (2LL * row + 1) * orientation->height / (2 * RASTER_SIZE)};
      if (containsPoint(orientation->outer, local))
        result.channels[2 * RASTER_SIZE * RASTER_SIZE + row * RASTER_SIZE + column] = 1.0F;
    }
  const std::vector<PolygonAction> candidates = environment_->enumerateCandidates(state_, instancePosition);
  for (const PolygonAction & action : candidates)
  {
    if (action.rotationDegrees != rotationDegrees)
      continue;
    result.actions.push_back(action);
    const int column = std::clamp(
      static_cast<int>((action.x - environment_->sheetMargin()) * RASTER_SIZE / std::max<std::int64_t>(1, usableWidth)), 0,
      RASTER_SIZE - 1);
    const int row = std::clamp(
      static_cast<int>((action.y - environment_->sheetMargin()) * RASTER_SIZE / std::max<std::int64_t>(1, usableHeight)), 0,
      RASTER_SIZE - 1);
    result.channels[3 * RASTER_SIZE * RASTER_SIZE + row * RASTER_SIZE + column] = 1.0F;
    result.candidateFeatures.insert(
      result.candidateFeatures.end(),
      {static_cast<float>(action.x / static_cast<double>(environment_->sheetWidth())),
       static_cast<float>(action.y / static_cast<double>(environment_->sheetHeight())),
       static_cast<float>(orientation->width / static_cast<double>(environment_->sheetWidth())),
       static_cast<float>(orientation->height / static_cast<double>(environment_->sheetHeight())),
       static_cast<float>(orientation->materialArea /
                          (static_cast<double>(environment_->sheetWidth()) * environment_->sheetHeight())),
       static_cast<float>((action.x + orientation->width) / static_cast<double>(environment_->sheetWidth())),
       static_cast<float>((action.y + orientation->height) / static_cast<double>(environment_->sheetHeight()))});
  }
  return result;
}

/// Применяет индекс каталога, используя изменение целевой функции как вознаграждение.
PolygonLearningCompactStepResult PolygonLearningEnvironment::applyStep(std::size_t actionIndex)
{
  if (isTerminal())
    throw std::logic_error("полигональный эпизод уже завершён");
  if (actionIndex >= actions_.size())
    throw std::out_of_range("индекс полигонального действия находится вне текущего каталога");
  std::array<double, 5> beforeComponents{};
  const double before = rewardPotential(*environment_, state_, config_.rewardVersion, beforeComponents);
  const PolygonAction action = actions_[actionIndex];
  if (!environment_->apply(state_, action))
    throw std::invalid_argument("полигональное действие больше не является допустимым");
  rebuildActions();
  std::array<double, 5> afterComponents{};
  const double after = rewardPotential(*environment_, state_, config_.rewardVersion, afterComponents);
  PolygonLearningCompactStepResult result;
  result.observation = dynamicObservation();
  result.reward = after - before;
  result.complete = isComplete();
  result.deadEnd = !result.complete && actions_.empty();
  result.terminated = result.complete || result.deadEnd;
  result.rewardVersion = config_.rewardVersion;
  result.potentialBefore = before;
  result.potentialAfter = after;
  result.componentDeltas.reserve(beforeComponents.size());
  for (std::size_t index = 0; index < beforeComponents.size(); ++index)
    result.componentDeltas.push_back(afterComponents[index] - beforeComponents[index]);
  return result;
}

/// Применяет общий компактный переход и добавляет совместимое полное наблюдение.
PolygonLearningStepResult PolygonLearningEnvironment::step(std::size_t actionIndex)
{
  PolygonLearningCompactStepResult compact = applyStep(actionIndex);
  PolygonLearningStepResult result;
  result.observation = observation();
  result.reward = compact.reward;
  result.terminated = compact.terminated;
  result.complete = compact.complete;
  result.deadEnd = compact.deadEnd;
  result.rewardVersion = compact.rewardVersion;
  result.potentialBefore = compact.potentialBefore;
  result.potentialAfter = compact.potentialAfter;
  result.componentDeltas = std::move(compact.componentDeltas);
  return result;
}

/// Возвращает результат общего перехода без построения полного совместимого наблюдения.
PolygonLearningCompactStepResult PolygonLearningEnvironment::stepCompact(std::size_t actionIndex)
{
  return applyStep(actionIndex);
}

/// Получает размещения и пересчитывает целевую функцию точной средой.
PolygonSolution PolygonLearningEnvironment::solution(const SolverMetadata & metadata) const
{
  PolygonSolution result;
  result.wireVersion = metadata.family == SolverFamily::Baseline ? 1 : 2;
  result.problemId = environment_->problem().problemId;
  result.status = isComplete() ? SolveStatus::Solved : SolveStatus::NoSolutionFound;
  result.placements = state_.placements;
  result.objective = environment_->evaluate(state_);
  result.solver = metadata;
  return result;
}

/// Сравнивает число применённых размещений с числом обязательных экземпляров.
bool PolygonLearningEnvironment::isComplete() const
{
  return state_.placements.size() == environment_->instances().size();
}

/// Завершает эпизод при полноте либо пустом динамическом пространстве действий.
bool PolygonLearningEnvironment::isTerminal() const
{
  return isComplete() || actions_.empty();
}
} // namespace aipackaging::solver
