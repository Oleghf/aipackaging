#include <memory>
#include <stdexcept>
#include <string>

#include <aipackaging/nesting/grid_io.h>
#include <aipackaging/nesting/grid_learning.h>
#include <aipackaging/nesting/grid_solver.h>
#include <pybind11/stl.h>

#include "bindingsinternal.h"

namespace aipackaging::python
{
using namespace solver;

namespace
{
/// Возвращает публичное Python-представление действия без раскрытия C++-типов.
py::dict actionToDict(const GridAction & action)
{
  py::dict result;
  result["partId"] = action.partId;
  result["instanceIndex"] = action.instanceIndex;
  result["column"] = action.column;
  result["row"] = action.row;
  result["rotationDegrees"] = action.rotationDegrees;
  return result;
}

/// Строго читает словарь действия для аудита и повторного проигрывания траектории.
GridAction actionFromDict(const py::dict & value)
{
  static const std::vector<std::string> required = {"partId", "instanceIndex", "column", "row", "rotationDegrees"};
  for (const std::string & field : required)
  {
    if (!value.contains(py::str(field)))
      throw py::value_error("в действии отсутствует обязательное поле: " + field);
  }
  if (value.size() != required.size())
    throw py::value_error("действие содержит неизвестные поля");
  return {value["partId"].cast<std::string>(), value["instanceIndex"].cast<std::uint32_t>(), value["column"].cast<int>(),
          value["row"].cast<int>(), value["rotationDegrees"].cast<int>()};
}

/// Преобразует плоский снимок C++ в массивы наблюдения NumPy v1 только для чтения.
py::dict observationToDict(const GridLearningObservation & observation)
{
  const py::ssize_t rows = observation.rows;
  const py::ssize_t columns = observation.columns;
  const py::ssize_t instances = static_cast<py::ssize_t>(observation.instanceCount);
  const py::ssize_t actions = static_cast<py::ssize_t>(observation.actionCount);
  py::dict result;
  result["occupancy"] = readonlyArray(observation.occupancy, {rows, columns});
  result["part_masks"] =
    readonlyArray(observation.partMasks, {instances, 4, observation.maxPartRows, observation.maxPartColumns});
  result["orientation_mask"] = readonlyBoolArray(observation.orientationMask, {instances, 4});
  result["remaining"] = readonlyBoolArray(observation.remaining, {instances});
  result["part_features"] = readonlyArray(observation.partFeatures, {instances, 7});
  result["candidate_instance"] = readonlyArray(observation.candidateInstance, {actions});
  result["candidate_rotation"] = readonlyArray(observation.candidateRotation, {actions});
  result["candidate_features"] = readonlyArray(observation.candidateFeatures, {actions, 7});
  result["action_mask"] = readonlyBoolArray(observation.actionMask, {actions});
  result["objective"] = readonlyArray(observation.objective, {7});
  return result;
}

/// Преобразует постоянную часть наблюдения во владеющие массивы NumPy только для чтения.
py::dict staticObservationToDict(const GridLearningStaticObservation & observation)
{
  const py::ssize_t instances = static_cast<py::ssize_t>(observation.instanceCount);
  const py::ssize_t actions = static_cast<py::ssize_t>(observation.actionCount);
  py::dict result;
  result["rows"] = observation.rows;
  result["columns"] = observation.columns;
  result["max_part_rows"] = observation.maxPartRows;
  result["max_part_columns"] = observation.maxPartColumns;
  result["part_masks"] =
    readonlyArray(observation.partMasks, {instances, 4, observation.maxPartRows, observation.maxPartColumns});
  result["orientation_mask"] = readonlyBoolArray(observation.orientationMask, {instances, 4});
  result["part_features"] = readonlyArray(observation.partFeatures, {instances, 7});
  result["candidate_instance"] = readonlyArray(observation.candidateInstance, {actions});
  result["candidate_rotation"] = readonlyArray(observation.candidateRotation, {actions});
  result["candidate_features"] = readonlyArray(observation.candidateFeatures, {actions, 7});
  return result;
}

/// Преобразует динамическую часть наблюдения во владеющие массивы NumPy только для чтения.
py::dict dynamicObservationToDict(const GridLearningDynamicObservation & observation, int rows, int columns)
{
  py::dict result;
  result["occupancy"] = readonlyArray(observation.occupancy, {rows, columns});
  result["remaining"] = readonlyBoolArray(observation.remaining, {static_cast<py::ssize_t>(observation.remaining.size())});
  result["action_mask"] = readonlyBoolArray(observation.actionMask, {static_cast<py::ssize_t>(observation.actionMask.size())});
  result["objective"] = readonlyArray(observation.objective, {7});
  return result;
}

/// Формирует аудируемые сведения одного перехода с рангами и изменениями целевой функции.
template<typename StepResult>
py::dict stepInfo(const GridLearningEnvironment & environment, const StepResult & step)
{
  py::dict deltas;
  deltas["placedParts"] = step.rewardComponents.placedPartsDelta;
  deltas["placedCells"] = step.rewardComponents.placedCellsDelta;
  deltas["usedLength"] = step.rewardComponents.usedLengthDelta;
  deltas["largestExtraRectangleArea"] = step.rewardComponents.largestExtraRectangleAreaDelta;
  deltas["fragmentationPenalty"] = step.rewardComponents.fragmentationPenaltyDelta;

  py::dict result;
  result["rankBefore"] = step.rewardComponents.rankBefore;
  result["rankAfter"] = step.rewardComponents.rankAfter;
  result["rankUpperBound"] = environment.rankUpperBound();
  result["deltas"] = std::move(deltas);
  result["complete"] = step.complete;
  result["deadEnd"] = step.deadEnd;
  return result;
}

/// Загружает строгую клеточную задачу `grid_problem` v1 и сообщает вызывающему коду Python точную причину отказа.
GridProblem parseProblem(const std::string & problemJson)
{
  GridProblemLoadResult loaded = loadGridProblemFromText(problemJson);
  if (!loaded.success)
    throw py::value_error(loaded.error);
  return std::move(loaded.problem);
}

/// Создаёт обучаемую среду из JSON с отдельно настраиваемыми ограничениями v1.
std::unique_ptr<GridLearningEnvironment> createEnvironment(const std::string & problemJson, std::size_t maxSheetArea,
                                                           std::size_t maxActions)
{
  GridProblem problem = parseProblem(problemJson);
  std::string error;
  std::unique_ptr<GridLearningEnvironment> result = GridLearningEnvironment::Create(problem, {maxSheetArea, maxActions}, error);
  if (!result)
    throw py::value_error(error);
  return result;
}

/// Запускает базовый алгоритм M1 и возвращает детерминированный JSON для набора данных.
// Порядок однотипных параметров закреплён именованным Python API py::arg ниже;
// объединение их в DTO только усложнит низкоуровневую границу модуля.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string solveProblem(const std::string & problemJson, const std::string & solverName, std::uint64_t seed,
                         std::size_t randomIterations, std::size_t beamWidth, std::size_t maxExpandedStates,
                         std::uint64_t timeoutMs)
{
  GridProblem problem = parseProblem(problemJson);
  SolverConfig config;
  if (!parseSolverKind(solverName, config.solver))
    throw py::value_error("неизвестный решатель: " + solverName);
  config.seed = seed;
  config.randomIterations = randomIterations;
  config.beamWidth = beamWidth;
  config.maxExpandedStates = maxExpandedStates;
  config.timeoutMs = timeoutMs;
  GridSolution solution = solveGridProblem(problem, config);

  // Времена зависят от загрузки машины и не должны разрушать побайтовую
  // воспроизводимость датасета. Счётчики поиска при этом сохраняются.
  solution.metrics.candidateGenerationTimeUs = 0;
  solution.metrics.validationTimeUs = 0;
  solution.metrics.searchTimeUs = 0;
  solution.metrics.totalTimeUs = 0;
  return saveGridSolutionToText(solution);
}

/// Проверяет пару задачи и решения независимым валидатором M1 и возвращает ошибку или пустую строку.
// Оба JSON различаются публичными именами `py::arg` и проходят разные строгие анализаторы.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string validateSolution(const std::string & problemJson, const std::string & solutionJson)
{
  const GridProblem problem = parseProblem(problemJson);
  const GridSolutionLoadResult loaded = loadGridSolutionFromText(solutionJson);
  if (!loaded.success)
    return loaded.error;
  const ValidationResult result = validateGridSolution(problem, loaded.solution);
  return result.success ? std::string{} : result.error;
}

/// Сравнивает два решения формата обмена общим компаратором целевой функции C++.
bool isBetterSolution(const std::string & candidateJson, const std::string & referenceJson)
{
  const GridSolutionLoadResult candidate = loadGridSolutionFromText(candidateJson);
  if (!candidate.success)
    throw py::value_error("некорректное проверяемое решение: " + candidate.error);
  const GridSolutionLoadResult reference = loadGridSolutionFromText(referenceJson);
  if (!reference.success)
    throw py::value_error("некорректное эталонное решение: " + reference.error);
  if (candidate.solution.problemId != reference.solution.problemId)
    throw py::value_error("решения относятся к разным задачам");
  return isBetterGridSolution(candidate.solution, reference.solution);
}

} // namespace

/// Регистрирует публичный клеточный API без изменения имён и значений по умолчанию.
void bindGrid(py::module_ & module)
{
  py::class_<GridLearningEnvironment>(module, "GridLearningEnvironment")
    .def("reset", [](GridLearningEnvironment & environment) { return observationToDict(environment.reset()); })
    .def("reset_compact", [](GridLearningEnvironment & environment)
         { return dynamicObservationToDict(environment.resetCompact(), environment.rows(), environment.columns()); })
    .def("static_observation",
         [](const GridLearningEnvironment & environment) { return staticObservationToDict(environment.staticObservation()); })
    .def("dynamic_observation", [](const GridLearningEnvironment & environment)
         { return dynamicObservationToDict(environment.dynamicObservation(), environment.rows(), environment.columns()); })
    .def("observation", [](const GridLearningEnvironment & environment) { return observationToDict(environment.observation()); })
    .def("step",
         [](GridLearningEnvironment & environment, std::size_t index)
         {
           const GridLearningStepResult result = environment.step(index);
           return py::make_tuple(observationToDict(result.observation), result.reward, result.terminated, false,
                                 stepInfo(environment, result));
         })
    .def("step_compact",
         [](GridLearningEnvironment & environment, std::size_t index)
         {
           const GridLearningCompactStepResult result = environment.stepCompact(index);
           return py::make_tuple(dynamicObservationToDict(result.observation, environment.rows(), environment.columns()),
                                 result.reward, result.terminated, false, stepInfo(environment, result));
         })
    .def(
      "snapshot_solution",
      [](const GridLearningEnvironment & environment, const py::dict & provenance, const std::string & incompleteStatus)
      {
        SolveStatus status;
        if (!parseSolveStatus(incompleteStatus, status))
          throw py::value_error("неизвестный статус неполного решения: " + incompleteStatus);
        return saveGridSolutionToText(environment.snapshotSolution(metadataFromDict(provenance), status));
      },
      py::arg("provenance"), py::arg("incomplete_status") = "budget_exhausted")
    .def("action",
         [](const GridLearningEnvironment & environment, std::size_t index) { return actionToDict(environment.action(index)); })
    .def("find_action",
         [](const GridLearningEnvironment & environment, const py::dict & action)
         {
           const std::size_t index = environment.findAction(actionFromDict(action));
           if (index == environment.actionCount())
             throw py::value_error("действие отсутствует в стабильном каталоге");
           return index;
         })
    .def_property_readonly("action_count", &GridLearningEnvironment::actionCount)
    .def_property_readonly("is_complete", &GridLearningEnvironment::isComplete)
    .def_property_readonly("is_terminal", &GridLearningEnvironment::isTerminal)
    .def_property_readonly("is_dead_end", &GridLearningEnvironment::isDeadEnd)
    .def_property_readonly("rank", &GridLearningEnvironment::rank)
    .def_property_readonly("rank_upper_bound", &GridLearningEnvironment::rankUpperBound)
    .def_property_readonly("problem_id", &GridLearningEnvironment::problemId);

  module.def("create_environment", &createEnvironment, py::arg("problem_json"), py::arg("max_sheet_area") = 4096,
             py::arg("max_actions") = 1000000);
  module.def("solve_problem", &solveProblem, py::arg("problem_json"), py::arg("solver") = "area-left-bottom",
             py::arg("seed") = 42, py::arg("random_iterations") = 64, py::arg("beam_width") = 32,
             py::arg("max_expanded_states") = 50000, py::arg("timeout_ms") = 0);
  module.def("validate_solution", &validateSolution, py::arg("problem_json"), py::arg("solution_json"));
  module.def("is_better_solution", &isBetterSolution, py::arg("candidate_json"), py::arg("reference_json"));
}
} // namespace aipackaging::python
