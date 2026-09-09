#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <set>
#include <utility>

#include <gridenvironment.h>

namespace aipackaging::solver
{
namespace
{
using CellKey = std::pair<int, int>;

/// Поворачивает клетки вокруг начала координат, переносит результат в первый квадрант и сортирует его.
std::vector<GridCell> normalizedRotation(const std::vector<GridCell> & cells, int rotationDegrees)
{
  std::vector<GridCell> result;
  result.reserve(cells.size());
  for (const GridCell & cell : cells)
  {
    GridCell rotated = cell;
    switch (rotationDegrees)
    {
      case 0:
        break;
      case 90:
        rotated = {-cell.row, cell.column};
        break;
      case 180:
        rotated = {-cell.column, -cell.row};
        break;
      case 270:
        rotated = {cell.row, -cell.column};
        break;
      default:
        return {};
    }
    result.push_back(rotated);
  }

  // После поворота координаты могут стать отрицательными. Сдвиг к (0, 0) делает
  // ориентации сопоставимыми и позволяет удалять симметричные дубликаты.
  const int minColumn = std::min_element(result.begin(), result.end(),
                                         [](const GridCell & lhs, const GridCell & rhs) { return lhs.column < rhs.column; })
                          ->column;
  const int minRow =
    std::min_element(result.begin(), result.end(), [](const GridCell & lhs, const GridCell & rhs) { return lhs.row < rhs.row; })
      ->row;
  for (GridCell & cell : result)
  {
    cell.column -= minColumn;
    cell.row -= minRow;
  }
  std::sort(result.begin(), result.end(), [](const GridCell & lhs, const GridCell & rhs)
            { return lhs.row == rhs.row ? lhs.column < rhs.column : lhs.row < rhs.row; });
  return result;
}

/// Обходит соседние клетки и проверяет, образует ли деталь одну 4-связную компоненту.
bool isConnected(const std::set<CellKey> & occupied)
{
  if (occupied.empty())
    return false;

  std::set<CellKey> visited;
  std::queue<CellKey> pending;
  pending.push(*occupied.begin());
  visited.insert(*occupied.begin());
  constexpr int OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  while (!pending.empty())
  {
    const CellKey current = pending.front();
    pending.pop();
    for (const auto & offset : OFFSETS)
    {
      const CellKey next{current.first + offset[0], current.second + offset[1]};
      if (occupied.contains(next) && visited.insert(next).second)
        pending.push(next);
    }
  }
  return visited.size() == occupied.size();
}

/// Заливает внешний фон расширенного bounding box и обнаруживает недоступные из него пустые клетки.
bool hasHole(const std::set<CellKey> & occupied, int width, int height)
{
  const int extendedWidth = width + 2;
  const int extendedHeight = height + 2;
  std::vector<unsigned char> blocked(static_cast<std::size_t>(extendedWidth) * extendedHeight, 0);
  for (const CellKey & cell : occupied)
    blocked[static_cast<std::size_t>(cell.second + 1) * extendedWidth + cell.first + 1] = 1;

  // Рамка в одну клетку гарантирует общую стартовую точку внешнего фона даже
  // тогда, когда деталь полностью занимает границу собственного bounding box.
  std::vector<unsigned char> visited(blocked.size(), 0);
  std::queue<CellKey> pending;
  pending.push({0, 0});
  visited[0] = 1;
  constexpr int OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  while (!pending.empty())
  {
    const CellKey current = pending.front();
    pending.pop();
    for (const auto & offset : OFFSETS)
    {
      const int column = current.first + offset[0];
      const int row = current.second + offset[1];
      if (column < 0 || row < 0 || column >= extendedWidth || row >= extendedHeight)
        continue;
      const std::size_t index = static_cast<std::size_t>(row) * extendedWidth + column;
      if (!blocked[index] && !visited[index])
      {
        visited[index] = 1;
        pending.push({column, row});
      }
    }
  }

  for (int row = 1; row <= height; ++row)
  {
    for (int column = 1; column <= width; ++column)
    {
      const std::size_t index = static_cast<std::size_t>(row) * extendedWidth + column;
      if (!blocked[index] && !visited[index])
        return true;
    }
  }
  return false;
}

/// Находит крупнейший свободный осевой прямоугольник алгоритмом гистограмм по строкам.
std::size_t largestFreeRectangle(const GridState & state, int columns, int rows, int usedLength)
{
  if (usedLength <= 0)
    return 0;

  std::vector<int> heights(static_cast<std::size_t>(usedLength), 0);
  std::size_t best = 0;
  for (int row = 0; row < rows; ++row)
  {
    for (int column = 0; column < usedLength; ++column)
    {
      const std::size_t index = static_cast<std::size_t>(row) * columns + column;
      heights[static_cast<std::size_t>(column)] = state.occupancy[index] ? 0 : heights[static_cast<std::size_t>(column)] + 1;
    }

    // Монотонный стек вычисляет лучший прямоугольник для текущей нижней строки
    // за линейное время по ширине занятой зоны.
    std::vector<int> stack;
    for (int column = 0; column <= usedLength; ++column)
    {
      const int currentHeight = column == usedLength ? 0 : heights[static_cast<std::size_t>(column)];
      while (!stack.empty() && heights[static_cast<std::size_t>(stack.back())] > currentHeight)
      {
        const int height = heights[static_cast<std::size_t>(stack.back())];
        stack.pop_back();
        const int left = stack.empty() ? 0 : stack.back() + 1;
        best = std::max(best, static_cast<std::size_t>(height) * static_cast<std::size_t>(column - left));
      }
      if (column < usedLength)
        stack.push_back(column);
    }
  }
  return best;
}

/// Считает суммарную свободную площадь и крупнейшую 4-связную компоненту в занятой по X зоне.
std::pair<std::size_t, std::size_t> freeComponents(const GridState & state, int columns, int rows, int usedLength)
{
  std::vector<unsigned char> visited(state.occupancy.size(), 0);
  std::size_t freeArea = 0;
  std::size_t largest = 0;
  constexpr int OFFSETS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  for (int row = 0; row < rows; ++row)
  {
    for (int column = 0; column < usedLength; ++column)
    {
      const std::size_t start = static_cast<std::size_t>(row) * columns + column;
      if (state.occupancy[start] || visited[start])
        continue;

      std::size_t area = 0;
      std::queue<CellKey> pending;
      pending.push({column, row});
      visited[start] = 1;
      while (!pending.empty())
      {
        const CellKey current = pending.front();
        pending.pop();
        ++area;
        for (const auto & offset : OFFSETS)
        {
          const int nextColumn = current.first + offset[0];
          const int nextRow = current.second + offset[1];
          if (nextColumn < 0 || nextRow < 0 || nextColumn >= usedLength || nextRow >= rows)
            continue;
          const std::size_t next = static_cast<std::size_t>(nextRow) * columns + nextColumn;
          if (!state.occupancy[next] && !visited[next])
          {
            visited[next] = 1;
            pending.push({nextColumn, nextRow});
          }
        }
      }
      freeArea += area;
      largest = std::max(largest, area);
    }
  }
  return {freeArea, largest};
}

/// Сравнивает записанные компоненты objective с независимо пересчитанными значениями.
bool equalObjective(const ObjectiveComponents & lhs, const ObjectiveComponents & rhs)
{
  return lhs.usedLength == rhs.usedLength && lhs.primaryRemnantWidth == rhs.primaryRemnantWidth &&
         lhs.largestExtraRectangleArea == rhs.largestExtraRectangleArea && lhs.fragmentationPenalty == rhs.fragmentationPenalty &&
         lhs.placedParts == rhs.placedParts && lhs.totalParts == rhs.totalParts && lhs.placedCells == rhs.placedCells &&
         lhs.totalPartCells == rhs.totalPartCells && std::abs(lhs.materialUtilization - rhs.materialUtilization) < 1e-12;
}
} // namespace

/// Последовательно проверяет схему задачи и физические ограничения клеточного MVP.
ValidationResult validateGridProblem(const GridProblem & problem)
{
  if (problem.problemId.empty())
    return {false, "problemId must not be empty"};
  if (problem.sheet.columns <= 0 || problem.sheet.rows <= 0)
    return {false, "sheet dimensions must be positive"};
  if (problem.sheet.unit != "cell")
    return {false, "only sheet unit 'cell' is supported"};
  if (problem.objective.type != "valuable_right_remnant" || problem.objective.version != 1)
    return {false, "unsupported objective"};
  if (static_cast<std::size_t>(problem.sheet.columns) >
      std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(problem.sheet.rows))
  {
    return {false, "sheet dimensions overflow addressable memory"};
  }

  std::set<std::string> partIds;
  for (const GridPart & part : problem.parts)
  {
    if (part.id.empty() || !partIds.insert(part.id).second)
      return {false, "part ids must be non-empty and unique"};
    if (part.quantity == 0)
      return {false, "part quantity must be positive"};
    if (part.cells.empty())
      return {false, "part cells must not be empty"};
    if (part.allowedRotations.empty())
      return {false, "allowedRotations must not be empty"};

    std::set<int> rotations;
    for (const int rotation : part.allowedRotations)
    {
      if ((rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) || !rotations.insert(rotation).second)
        return {false, "allowedRotations must contain unique quarter turns"};
    }

    // Множество одновременно выявляет дубликаты и служит индексом для обхода
    // связности и поиска внутренних пустот.
    std::set<CellKey> occupied;
    int minColumn = std::numeric_limits<int>::max();
    int minRow = std::numeric_limits<int>::max();
    int maxColumn = 0;
    int maxRow = 0;
    for (const GridCell & cell : part.cells)
    {
      if (cell.column < 0 || cell.row < 0 || !occupied.insert({cell.column, cell.row}).second)
        return {false, "part cells must be unique and non-negative"};
      minColumn = std::min(minColumn, cell.column);
      minRow = std::min(minRow, cell.row);
      maxColumn = std::max(maxColumn, cell.column);
      maxRow = std::max(maxRow, cell.row);
    }
    if (minColumn != 0 || minRow != 0)
      return {false, "part cells must be normalized to both axes"};
    if (!isConnected(occupied))
      return {false, "part cells must be 4-connected"};
    if (hasHole(occupied, maxColumn + 1, maxRow + 1))
      return {false, "parts with holes are not supported in grid problem v1"};
  }
  return {true, {}};
}

/// Конструирует среду из данных, которые уже прошли публичную валидацию.
GridEnvironment::GridEnvironment(GridProblem problem, std::vector<std::vector<GridOrientation>> orientations,
                                 std::vector<GridPartInstance> instances)
  : problem_(std::move(problem))
  , orientations_(std::move(orientations))
  , instances_(std::move(instances))
{
}

/// Проверяет задачу, строит уникальные ориентации и разворачивает quantity в стабильные экземпляры.
std::unique_ptr<GridEnvironment> GridEnvironment::Create(const GridProblem & problem, std::string & error)
{
  const ValidationResult validation = validateGridProblem(problem);
  if (!validation.success)
  {
    error = validation.error;
    return nullptr;
  }

  std::vector<std::vector<GridOrientation>> allOrientations;
  std::vector<GridPartInstance> instances;
  allOrientations.reserve(problem.parts.size());
  for (std::size_t partIndex = 0; partIndex < problem.parts.size(); ++partIndex)
  {
    const GridPart & part = problem.parts[partIndex];
    std::vector<int> rotations = part.allowedRotations;
    std::sort(rotations.begin(), rotations.end());
    std::vector<GridOrientation> partOrientations;
    for (const int rotation : rotations)
    {
      std::vector<GridCell> cells = normalizedRotation(part.cells, rotation);
      // Сравнение нормализованных наборов удаляет лишние действия для
      // симметричных деталей, сохраняя наименьший разрешённый угол.
      const bool duplicate = std::any_of(partOrientations.begin(), partOrientations.end(),
                                         [&cells](const GridOrientation & value) { return value.cells == cells; });
      if (duplicate)
        continue;
      const int width = std::max_element(cells.begin(), cells.end(),
                                         [](const GridCell & lhs, const GridCell & rhs) { return lhs.column < rhs.column; })
                          ->column +
                        1;
      const int height =
        std::max_element(cells.begin(), cells.end(), [](const GridCell & lhs, const GridCell & rhs) { return lhs.row < rhs.row; })
          ->row +
        1;
      partOrientations.push_back({rotation, width, height, std::move(cells)});
    }
    allOrientations.push_back(std::move(partOrientations));

    int maxDimension = 0;
    for (const GridOrientation & orientation : allOrientations.back())
      maxDimension = std::max({maxDimension, orientation.width, orientation.height});
    for (std::uint32_t instanceIndex = 0; instanceIndex < part.quantity; ++instanceIndex)
      instances.push_back({partIndex, instanceIndex, part.cells.size(), maxDimension});
  }
  error.clear();
  return std::unique_ptr<GridEnvironment>(new GridEnvironment(problem, std::move(allOrientations), std::move(instances)));
}

/// Возвращает подготовленный список ориентаций с проверкой индекса через at().
const std::vector<GridOrientation> & GridEnvironment::orientations(std::size_t partIndex) const
{
  return orientations_.at(partIndex);
}

/// Выделяет пустую occupancy-матрицу и маску ещё не размещённых экземпляров.
GridState GridEnvironment::initialState() const
{
  GridState state;
  state.occupancy.resize(static_cast<std::size_t>(problem_.sheet.columns) * problem_.sheet.rows, 0);
  state.placedInstances.resize(instances_.size(), 0);
  return state;
}

/// Перебирает ориентации и все позиции их bounding box в стабильном порядке rotation/column/row.
std::vector<GridAction> GridEnvironment::enumerateCandidates(const GridState &, std::size_t instancePosition) const
{
  std::vector<GridAction> result;
  if (instancePosition >= instances_.size())
    return result;
  const GridPartInstance & instance = instances_[instancePosition];
  const GridPart & part = problem_.parts[instance.partIndex];
  for (const GridOrientation & orientation : orientations_[instance.partIndex])
  {
    if (orientation.width > problem_.sheet.columns || orientation.height > problem_.sheet.rows)
      continue;
    for (int column = 0; column <= problem_.sheet.columns - orientation.width; ++column)
    {
      for (int row = 0; row <= problem_.sheet.rows - orientation.height; ++row)
        result.push_back({part.id, instance.instanceIndex, column, row, orientation.rotationDegrees});
    }
  }
  return result;
}

/// Линейно сопоставляет публичный идентификатор размещения внутреннему экземпляру.
std::size_t GridEnvironment::findInstance(const std::string & partId, std::uint32_t instanceIndex) const
{
  for (std::size_t index = 0; index < instances_.size(); ++index)
  {
    const GridPartInstance & instance = instances_[index];
    if (problem_.parts[instance.partIndex].id == partId && instance.instanceIndex == instanceIndex)
      return index;
  }
  return instances_.size();
}

/// Ищет нормализованную ориентацию, оставшуюся после удаления симметричных дублей.
const GridOrientation * GridEnvironment::findOrientation(std::size_t partIndex, int rotationDegrees) const
{
  if (partIndex >= orientations_.size())
    return nullptr;
  const auto it =
    std::find_if(orientations_[partIndex].begin(), orientations_[partIndex].end(),
                 [rotationDegrees](const GridOrientation & value) { return value.rotationDegrees == rotationDegrees; });
  return it == orientations_[partIndex].end() ? nullptr : &*it;
}

/// Проверяет принадлежность экземпляра, разрешённый угол, границы листа и свободные клетки.
bool GridEnvironment::canApply(const GridState & state, const GridAction & action) const
{
  const std::size_t instancePosition = findInstance(action.partId, action.instanceIndex);
  if (instancePosition >= instances_.size() || instancePosition >= state.placedInstances.size() ||
      state.placedInstances[instancePosition])
  {
    return false;
  }
  const GridPartInstance & instance = instances_[instancePosition];
  const GridOrientation * orientation = findOrientation(instance.partIndex, action.rotationDegrees);
  if (!orientation || action.column < 0 || action.row < 0 || action.column + orientation->width > problem_.sheet.columns ||
      action.row + orientation->height > problem_.sheet.rows)
  {
    return false;
  }
  for (const GridCell & cell : orientation->cells)
  {
    const std::size_t index = static_cast<std::size_t>(action.row + cell.row) * problem_.sheet.columns +
                              static_cast<std::size_t>(action.column + cell.column);
    if (state.occupancy[index])
      return false;
  }
  return true;
}

/// Повторно защищённо проверяет действие и атомарно отмечает все занятые им клетки.
bool GridEnvironment::apply(GridState & state, const GridAction & action) const
{
  if (!canApply(state, action))
    return false;
  const std::size_t instancePosition = findInstance(action.partId, action.instanceIndex);
  const GridPartInstance & instance = instances_[instancePosition];
  const GridOrientation * orientation = findOrientation(instance.partIndex, action.rotationDegrees);
  for (const GridCell & cell : orientation->cells)
  {
    const std::size_t index = static_cast<std::size_t>(action.row + cell.row) * problem_.sheet.columns +
                              static_cast<std::size_t>(action.column + cell.column);
    state.occupancy[index] = 1;
  }
  state.placedInstances[instancePosition] = 1;
  state.placements.push_back(action);
  state.placedCells += orientation->cells.size();
  return true;
}

/// Сканирует occupancy и вычисляет правую полосу, дополнительный прямоугольник и фрагментацию.
ObjectiveComponents GridEnvironment::evaluate(const GridState & state) const
{
  ObjectiveComponents result;
  result.totalParts = instances_.size();
  result.placedParts = state.placements.size();
  result.placedCells = state.placedCells;
  for (const GridPartInstance & instance : instances_)
    result.totalPartCells += instance.area;

  // Правый край крайней занятой колонки определяет основную бизнес-метрику.
  for (int column = problem_.sheet.columns - 1; column >= 0; --column)
  {
    bool occupied = false;
    for (int row = 0; row < problem_.sheet.rows; ++row)
    {
      if (state.occupancy[static_cast<std::size_t>(row) * problem_.sheet.columns + column])
      {
        occupied = true;
        break;
      }
    }
    if (occupied)
    {
      result.usedLength = column + 1;
      break;
    }
  }
  result.primaryRemnantWidth = problem_.sheet.columns - result.usedLength;
  result.largestExtraRectangleArea = largestFreeRectangle(state, problem_.sheet.columns, problem_.sheet.rows, result.usedLength);
  // Правая прямоугольная полоса исключается из secondary metrics: они оценивают
  // только остатки внутри уже использованной длины листа.
  const auto [freeArea, largestComponent] = freeComponents(state, problem_.sheet.columns, problem_.sheet.rows, result.usedLength);
  result.fragmentationPenalty = freeArea - largestComponent;
  const std::size_t sheetArea = static_cast<std::size_t>(problem_.sheet.columns) * problem_.sheet.rows;
  result.materialUtilization = sheetArea == 0 ? 0.0 : static_cast<double>(state.placedCells) / static_cast<double>(sheetArea);
  return result;
}

/// Воспроизводит placements в чистой среде и сверяет полноту и все objective-компоненты.
ValidationResult validateGridSolution(const GridProblem & problem, const GridSolution & solution)
{
  std::string error;
  std::unique_ptr<GridEnvironment> environment = GridEnvironment::Create(problem, error);
  if (!environment)
    return {false, error};
  if (solution.problemId != problem.problemId)
    return {false, "solution problemId does not match problem"};
  if (solution.status == SolveStatus::InvalidProblem)
    return {false, "a valid problem cannot have invalid_problem solution status"};

  GridState state = environment->initialState();
  for (const GridPlacement & placement : solution.placements)
  {
    if (!environment->apply(state, placement))
      return {false, "solution contains an invalid placement"};
  }
  const bool allPlaced = state.placements.size() == environment->instances().size();
  if ((solution.status == SolveStatus::Solved) != allPlaced)
    return {false, "solution status does not match completeness"};

  const ObjectiveComponents actual = environment->evaluate(state);
  if (!equalObjective(actual, solution.objective))
    return {false, "solution objective components do not match placements"};
  return {true, {}};
}
} // namespace aipackaging::solver
