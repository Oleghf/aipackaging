#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <aipackaging/nesting/grid_learning.h>

namespace aipackaging::solver
{
namespace
{
constexpr std::size_t MAX_V1_SHEET_AREA = 4096;
constexpr std::size_t MAX_V1_ACTIONS = 1000000;

/// Умножает и прибавляет без переполнения типа `uint64_t`, используемого контрактом ранга v1.
std::uint64_t checkedMultiplyAdd(std::uint64_t value, std::uint64_t multiplier, std::uint64_t addition)
{
  if (multiplier != 0 && value > (std::numeric_limits<std::uint64_t>::max() - addition) / multiplier)
    throw std::overflow_error("переполнение ранга вознаграждения клеточной среды");
  return value * multiplier + addition;
}

/// Возвращает нормализованное значение, не выполняя деление на ноль.
float normalized(std::size_t value, std::size_t denominator)
{
  return denominator == 0 ? 0.0F : static_cast<float>(value) / static_cast<float>(denominator);
}

/// Возвращает знаковую разность двух беззнаковых компонент метрики.
std::int64_t signedDelta(std::size_t after, std::size_t before)
{
  return static_cast<std::int64_t>(after) - static_cast<std::int64_t>(before);
}
} // namespace

/// Проверяет M1-задачу и лимиты, затем последовательно строит общий каталог всех экземпляров.
std::unique_ptr<GridLearningEnvironment> GridLearningEnvironment::Create(const GridProblem & problem,
                                                                         const GridLearningLimits & limits, std::string & error)
{
  const ValidationResult validation = validateGridProblem(problem);
  if (!validation.success)
  {
    error = validation.error;
    return nullptr;
  }
  if (limits.maxSheetArea > MAX_V1_SHEET_AREA || limits.maxActions > MAX_V1_ACTIONS)
  {
    error = "learning environment limits cannot exceed v1 contract limits";
    return nullptr;
  }

  // Лимиты проверяются до создания GridEnvironment: M1 допускает значительно
  // большие листы и количества, для которых среда обучения не должна выделять память.
  const std::size_t sheetArea = static_cast<std::size_t>(problem.sheet.columns) * problem.sheet.rows;
  if (sheetArea > limits.maxSheetArea)
  {
    error = "learning environment v1 sheet area limit exceeded";
    return nullptr;
  }
  std::size_t instanceCount = 0;
  for (const GridPart & part : problem.parts)
  {
    if (part.quantity > sheetArea - instanceCount)
    {
      error = "learning environment v1 instance count exceeds sheet area";
      return nullptr;
    }
    instanceCount += part.quantity;
  }

  std::string environmentError;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, environmentError);
  if (!environment)
  {
    error = environmentError;
    return nullptr;
  }

  std::vector<GridAction> actions;
  const GridState initial = environment->initialState();
  for (std::size_t instance = 0; instance < environment->instances().size(); ++instance)
  {
    std::vector<GridAction> candidates = environment->enumerateCandidates(initial, instance);
    if (candidates.size() > limits.maxActions - std::min(limits.maxActions, actions.size()))
    {
      error = "learning environment v1 action catalog limit exceeded";
      return nullptr;
    }
    actions.insert(actions.end(), candidates.begin(), candidates.end());
  }

  try
  {
    auto result =
      std::unique_ptr<GridLearningEnvironment>(new GridLearningEnvironment(std::move(environment), limits, std::move(actions)));
    error.clear();
    return result;
  }
  catch (const std::exception & exception)
  {
    error = exception.what();
    return nullptr;
  }
}

/// Инициализирует постоянные признаки и вычисляет безопасный знаменатель смешанного ранга.
GridLearningEnvironment::GridLearningEnvironment(std::unique_ptr<GridEnvironment> environment, GridLearningLimits limits,
                                                 std::vector<GridAction> actions)
  : environment_(std::move(environment))
  , limits_(limits)
  , actions_(std::move(actions))
  , state_(environment_->initialState())
{
  staticObservation_ = buildStaticObservation();
  const GridProblem & problem = environment_->problem();
  const std::size_t sheetArea = static_cast<std::size_t>(problem.sheet.columns) * problem.sheet.rows;
  ObjectiveComponents upper;
  upper.placedParts = environment_->instances().size();
  upper.placedCells = sheetArea;
  upper.usedLength = 0;
  upper.largestExtraRectangleArea = sheetArea;
  upper.fragmentationPenalty = 0;
  rankUpperBound_ = std::max<std::uint64_t>(1, calculateRank(upper));
  updateTerminal(buildActionMask());
}

/// Обнуляет занятость и размещения, после чего обновляет признаки завершения.
GridLearningObservation GridLearningEnvironment::reset()
{
  GridLearningDynamicObservation dynamic = resetCompact();
  GridLearningObservation result = staticObservation_;
  result.occupancy = std::move(dynamic.occupancy);
  result.remaining = std::move(dynamic.remaining);
  result.actionMask = std::move(dynamic.actionMask);
  result.objective = std::move(dynamic.objective);
  return result;
}

/// Сбрасывает изменяемое состояние и возвращает только поля, меняющиеся в ходе эпизода.
GridLearningDynamicObservation GridLearningEnvironment::resetCompact()
{
  state_ = environment_->initialState();
  std::vector<std::uint8_t> mask = buildActionMask();
  updateTerminal(mask);
  return buildDynamicObservation(std::move(mask));
}

/// Копирует только постоянные поля уже построенного кэша наблюдения.
GridLearningStaticObservation GridLearningEnvironment::staticObservation() const
{
  GridLearningStaticObservation result;
  result.rows = staticObservation_.rows;
  result.columns = staticObservation_.columns;
  result.maxPartRows = staticObservation_.maxPartRows;
  result.maxPartColumns = staticObservation_.maxPartColumns;
  result.instanceCount = staticObservation_.instanceCount;
  result.actionCount = staticObservation_.actionCount;
  result.partMasks = staticObservation_.partMasks;
  result.orientationMask = staticObservation_.orientationMask;
  result.partFeatures = staticObservation_.partFeatures;
  result.candidateInstance = staticObservation_.candidateInstance;
  result.candidateRotation = staticObservation_.candidateRotation;
  result.candidateFeatures = staticObservation_.candidateFeatures;
  return result;
}

/// Собирает только поля, зависящие от текущего изменяемого состояния среды.
GridLearningDynamicObservation GridLearningEnvironment::dynamicObservation() const
{
  return buildDynamicObservation(buildActionMask());
}

/// Объединяет изменяемое состояние, целевую функцию и переданную маску без повторной геометрической проверки.
GridLearningDynamicObservation GridLearningEnvironment::buildDynamicObservation(std::vector<std::uint8_t> actionMask) const
{
  GridLearningDynamicObservation result;
  result.occupancy = state_.occupancy;
  result.remaining.resize(environment_->instances().size(), 1);
  for (std::size_t index = 0; index < state_.placedInstances.size(); ++index)
    result.remaining[index] = state_.placedInstances[index] ? 0 : 1;
  result.actionMask = std::move(actionMask);

  const ObjectiveComponents objective = environment_->evaluate(state_);
  const std::size_t sheetArea = static_cast<std::size_t>(staticObservation_.rows) * staticObservation_.columns;
  result.objective = {
    normalized(objective.placedParts, objective.totalParts),
    normalized(objective.placedCells, sheetArea),
    normalized(static_cast<std::size_t>(objective.usedLength), static_cast<std::size_t>(staticObservation_.columns)),
    normalized(static_cast<std::size_t>(objective.primaryRemnantWidth), static_cast<std::size_t>(staticObservation_.columns)),
    normalized(objective.largestExtraRectangleArea, sheetArea),
    normalized(objective.fragmentationPenalty, sheetArea),
    static_cast<float>(objective.materialUtilization)};
  return result;
}

/// Объединяет кэшированные статические признаки с текущей занятостью, маской и целевой функцией.
GridLearningObservation GridLearningEnvironment::observation() const
{
  GridLearningObservation result = staticObservation_;
  GridLearningDynamicObservation dynamic = dynamicObservation();
  result.occupancy = std::move(dynamic.occupancy);
  result.remaining = std::move(dynamic.remaining);
  result.actionMask = std::move(dynamic.actionMask);
  result.objective = std::move(dynamic.objective);
  return result;
}

/// Проверяет индекс и маску, применяет действие и собирает общую компактную часть результата.
GridLearningCompactStepResult GridLearningEnvironment::applyStep(std::size_t actionIndex)
{
  if (terminal_)
    throw std::runtime_error("нельзя выполнить шаг завершённого клеточного эпизода обучения");
  if (actionIndex >= actions_.size())
    throw std::out_of_range("индекс действия клеточной среды вне допустимого диапазона");
  if (!environment_->canApply(state_, actions_[actionIndex]))
    throw std::invalid_argument("действие клеточной среды запрещено маской");

  const ObjectiveComponents before = environment_->evaluate(state_);
  const std::uint64_t rankBefore = calculateRank(before);
  if (!environment_->apply(state_, actions_[actionIndex]))
    throw std::logic_error("проверенное действие клеточной среды не удалось применить");
  const ObjectiveComponents after = environment_->evaluate(state_);
  const std::uint64_t rankAfter = calculateRank(after);
  std::vector<std::uint8_t> mask = buildActionMask();
  updateTerminal(mask);

  GridLearningCompactStepResult result;
  result.observation = buildDynamicObservation(std::move(mask));
  result.reward = static_cast<double>(rankAfter - rankBefore) / static_cast<double>(rankUpperBound_);
  result.terminated = terminal_;
  result.complete = complete_;
  result.deadEnd = isDeadEnd();
  result.rewardComponents = {rankBefore,
                             rankAfter,
                             signedDelta(after.placedParts, before.placedParts),
                             signedDelta(after.placedCells, before.placedCells),
                             after.usedLength - before.usedLength,
                             signedDelta(after.largestExtraRectangleArea, before.largestExtraRectangleArea),
                             signedDelta(after.fragmentationPenalty, before.fragmentationPenalty)};
  return result;
}

/// Делегирует изменение состояния компактному пути и добавляет массивы наблюдения v1.
GridLearningStepResult GridLearningEnvironment::step(std::size_t actionIndex)
{
  GridLearningCompactStepResult compact = applyStep(actionIndex);
  GridLearningStepResult result;
  result.observation = staticObservation_;
  result.observation.occupancy = std::move(compact.observation.occupancy);
  result.observation.remaining = std::move(compact.observation.remaining);
  result.observation.actionMask = std::move(compact.observation.actionMask);
  result.observation.objective = std::move(compact.observation.objective);
  result.reward = compact.reward;
  result.terminated = compact.terminated;
  result.complete = compact.complete;
  result.deadEnd = compact.deadEnd;
  result.rewardComponents = compact.rewardComponents;
  return result;
}

/// Возвращает результат общего пути без дорогостоящего копирования каталога действий.
GridLearningCompactStepResult GridLearningEnvironment::stepCompact(std::size_t actionIndex)
{
  return applyStep(actionIndex);
}

/// Копирует текущие размещения и вычисляет целевую функцию тем же кодом, что валидатор.
GridSolution GridLearningEnvironment::snapshotSolution(const SolverMetadata & solver, SolveStatus incompleteStatus) const
{
  if (incompleteStatus == SolveStatus::Solved || incompleteStatus == SolveStatus::InvalidProblem)
    throw std::invalid_argument("статус неполного снимка должен описывать корректный частичный результат");

  GridSolution result;
  result.wireVersion = solver.family == SolverFamily::Baseline ? 1 : 2;
  result.problemId = environment_->problem().problemId;
  result.status = complete_ ? SolveStatus::Solved : incompleteStatus;
  result.placements = state_.placements;
  result.objective = environment_->evaluate(state_);
  result.solver = solver;
  return result;
}

/// Делегирует проверку диапазона std::vector::at и возвращает неизменное действие каталога.
const GridAction & GridLearningEnvironment::action(std::size_t actionIndex) const
{
  return actions_.at(actionIndex);
}

/// Линейно сопоставляет сериализованное действие каталогу для повторного проигрывания.
std::size_t GridLearningEnvironment::findAction(const GridAction & actionValue) const
{
  const auto found = std::find(actions_.begin(), actions_.end(), actionValue);
  return found == actions_.end() ? actions_.size() : static_cast<std::size_t>(found - actions_.begin());
}

/// Пересчитывает целевую функцию и кодирует её поля смешанными основаниями в порядке M1.
std::uint64_t GridLearningEnvironment::rank() const
{
  return calculateRank(environment_->evaluate(state_));
}

/// Проверяет каждый элемент постоянного каталога через авторитетный M1-валидатор.
std::vector<std::uint8_t> GridLearningEnvironment::buildActionMask() const
{
  std::vector<std::uint8_t> result(actions_.size(), 0);
  for (std::size_t index = 0; index < actions_.size(); ++index)
    result[index] = environment_->canApply(state_, actions_[index]) ? 1 : 0;
  return result;
}

/// Определяет полноту по количеству, а тупик — по отсутствию единиц в готовой маске.
void GridLearningEnvironment::updateTerminal(const std::vector<std::uint8_t> & actionMask)
{
  complete_ = state_.placements.size() == environment_->instances().size();
  terminal_ = complete_ || std::none_of(actionMask.begin(), actionMask.end(), [](std::uint8_t value) { return value != 0; });
}

/// Кодирует лексикографический порядок в тип `uint64_t` последовательными проверяемыми операциями умножения и сложения.
std::uint64_t GridLearningEnvironment::calculateRank(const ObjectiveComponents & objective) const
{
  const GridProblem & problem = environment_->problem();
  const std::uint64_t sheetArea = static_cast<std::uint64_t>(problem.sheet.columns) * problem.sheet.rows;
  const std::uint64_t base = sheetArea + 1;
  std::uint64_t result = objective.placedParts;
  result = checkedMultiplyAdd(result, base, objective.placedCells);
  result = checkedMultiplyAdd(result, static_cast<std::uint64_t>(problem.sheet.columns) + 1,
                              static_cast<std::uint64_t>(problem.sheet.columns - objective.usedLength));
  result = checkedMultiplyAdd(result, base, objective.largestExtraRectangleArea);
  result = checkedMultiplyAdd(result, base, sheetArea - objective.fragmentationPenalty);
  return result;
}

/// Разворачивает ориентации и действия в плоские массивы с построчным размещением элементов.
GridLearningObservation GridLearningEnvironment::buildStaticObservation() const
{
  GridLearningObservation result;
  const GridProblem & problem = environment_->problem();
  result.rows = problem.sheet.rows;
  result.columns = problem.sheet.columns;
  result.instanceCount = environment_->instances().size();
  result.actionCount = actions_.size();

  for (std::size_t partIndex = 0; partIndex < problem.parts.size(); ++partIndex)
  {
    for (const GridOrientation & orientation : environment_->orientations(partIndex))
    {
      result.maxPartRows = std::max(result.maxPartRows, orientation.height);
      result.maxPartColumns = std::max(result.maxPartColumns, orientation.width);
    }
  }

  const std::size_t maskSize = result.instanceCount * 4 * static_cast<std::size_t>(result.maxPartRows) * result.maxPartColumns;
  result.partMasks.assign(maskSize, 0);
  result.orientationMask.assign(result.instanceCount * 4, 0);
  result.partFeatures.reserve(result.instanceCount * 7);
  const std::size_t sheetArea = static_cast<std::size_t>(result.rows) * result.columns;
  const std::size_t typeDenominator = problem.parts.size() > 1 ? problem.parts.size() - 1 : 1;

  for (std::size_t instancePosition = 0; instancePosition < result.instanceCount; ++instancePosition)
  {
    const GridPartInstance & instance = environment_->instances()[instancePosition];
    const GridPart & part = problem.parts[instance.partIndex];
    int maxWidth = 0;
    int maxHeight = 0;
    const auto & orientations = environment_->orientations(instance.partIndex);
    for (const GridOrientation & orientation : orientations)
    {
      maxWidth = std::max(maxWidth, orientation.width);
      maxHeight = std::max(maxHeight, orientation.height);
      const std::size_t rotationSlot = static_cast<std::size_t>(orientation.rotationDegrees / 90);
      result.orientationMask[instancePosition * 4 + rotationSlot] = 1;
      for (const GridCell & cell : orientation.cells)
      {
        const std::size_t index =
          (((instancePosition * 4 + rotationSlot) * result.maxPartRows + cell.row) * result.maxPartColumns) + cell.column;
        result.partMasks[index] = 1;
      }
    }
    const std::size_t instanceDenominator = part.quantity > 1 ? part.quantity - 1 : 1;
    result.partFeatures.insert(result.partFeatures.end(),
                               {normalized(instance.area, sheetArea),
                                normalized(static_cast<std::size_t>(maxWidth), static_cast<std::size_t>(result.columns)),
                                normalized(static_cast<std::size_t>(maxHeight), static_cast<std::size_t>(result.rows)),
                                normalized(orientations.size(), 4), normalized(instance.partIndex, typeDenominator),
                                normalized(instance.instanceIndex, instanceDenominator),
                                normalized(part.quantity, result.instanceCount)});
  }

  result.candidateInstance.reserve(actions_.size());
  result.candidateRotation.reserve(actions_.size());
  result.candidateFeatures.reserve(actions_.size() * 7);
  for (const GridAction & actionValue : actions_)
  {
    const std::size_t instancePosition = environment_->findInstance(actionValue.partId, actionValue.instanceIndex);
    const GridPartInstance & instance = environment_->instances()[instancePosition];
    const GridOrientation * orientation = environment_->findOrientation(instance.partIndex, actionValue.rotationDegrees);
    result.candidateInstance.push_back(static_cast<std::int32_t>(instancePosition));
    result.candidateRotation.push_back(static_cast<std::uint8_t>(actionValue.rotationDegrees / 90));
    result.candidateFeatures.insert(
      result.candidateFeatures.end(),
      {normalized(static_cast<std::size_t>(actionValue.column), result.columns),
       normalized(static_cast<std::size_t>(actionValue.row), result.rows),
       normalized(static_cast<std::size_t>(orientation->width), result.columns),
       normalized(static_cast<std::size_t>(orientation->height), result.rows), normalized(instance.area, sheetArea),
       normalized(static_cast<std::size_t>(actionValue.column) + static_cast<std::size_t>(orientation->width),
                  static_cast<std::size_t>(result.columns)),
       normalized(static_cast<std::size_t>(actionValue.row) + static_cast<std::size_t>(orientation->height),
                  static_cast<std::size_t>(result.rows))});
  }
  return result;
}
} // namespace aipackaging::solver
