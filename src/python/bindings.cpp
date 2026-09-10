#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gridio.h>
#include <gridlearning.h>
#include <gridsolver.h>
#include <polygonio.h>
#include <polygonlearning.h>
#include <polygonsolver.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

namespace
{
using namespace aipackaging::solver;

/// Создаёт владеющий NumPy-массив, копирует C++-данные и запрещает запись из Python.
template<typename T>
py::array_t<T> readonlyArray(const std::vector<T> & values, const std::vector<py::ssize_t> & shape)
{
  py::array_t<T> result(shape);
  if (!values.empty())
    std::memcpy(result.mutable_data(), values.data(), values.size() * sizeof(T));
  result.attr("setflags")(py::arg("write") = false);
  return result;
}

/// Преобразует byte-mask C++ в настоящий NumPy bool без общего изменяемого буфера.
py::array_t<bool> readonlyBoolArray(const std::vector<std::uint8_t> & values, const std::vector<py::ssize_t> & shape)
{
  py::array_t<bool> result(shape);
  bool * output = result.mutable_data();
  for (std::size_t index = 0; index < values.size(); ++index)
    output[index] = values[index] != 0;
  result.attr("setflags")(py::arg("write") = false);
  return result;
}

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

/// Строго читает словарь действия, используемый при аудите и replay траектории.
GridAction actionFromDict(const py::dict & value)
{
  static const std::vector<std::string> required = {"partId", "instanceIndex", "column", "row", "rotationDegrees"};
  for (const std::string & field : required)
  {
    if (!value.contains(py::str(field)))
      throw py::value_error("action is missing required field: " + field);
  }
  if (value.size() != required.size())
    throw py::value_error("action contains unknown fields");
  return {value["partId"].cast<std::string>(), value["instanceIndex"].cast<std::uint32_t>(), value["column"].cast<int>(),
          value["row"].cast<int>(), value["rotationDegrees"].cast<int>()};
}

/// Преобразует плоский C++-снимок в документированные read-only NumPy-массивы observation v1.
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

/// Преобразует постоянную часть observation в владеющие read-only NumPy-массивы.
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

/// Преобразует динамическую часть observation в владеющие read-only NumPy-массивы.
py::dict dynamicObservationToDict(const GridLearningDynamicObservation & observation, int rows, int columns)
{
  py::dict result;
  result["occupancy"] = readonlyArray(observation.occupancy, {rows, columns});
  result["remaining"] = readonlyBoolArray(observation.remaining, {static_cast<py::ssize_t>(observation.remaining.size())});
  result["action_mask"] = readonlyBoolArray(observation.actionMask, {static_cast<py::ssize_t>(observation.actionMask.size())});
  result["objective"] = readonlyArray(observation.objective, {7});
  return result;
}

/// Формирует аудируемый info одного перехода с рангами и дельтами objective.
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

/// Строго преобразует Python provenance в публичную C++ metadata solution v2.
SolverMetadata metadataFromDict(const py::dict & value)
{
  static const std::vector<std::string> required = {"family",           "name",      "projectVersion",    "revision",  "seed",
                                                    "randomIterations", "beamWidth", "maxExpandedStates", "timeoutMs", "modelId",
                                                    "modelSha256",      "rollouts",  "selectionMode"};
  for (const std::string & field : required)
  {
    if (!value.contains(py::str(field)))
      throw py::value_error("solver provenance is missing required field: " + field);
  }
  if (value.size() != required.size())
    throw py::value_error("solver provenance contains unknown fields");

  SolverMetadata result;
  const std::string family = value["family"].cast<std::string>();
  if (!parseSolverFamily(family, result.family))
    throw py::value_error("unknown solver family: " + family);
  result.name = value["name"].cast<std::string>();
  result.projectVersion = value["projectVersion"].cast<std::string>();
  result.revision = value["revision"].cast<std::string>();
  result.seed = value["seed"].cast<std::uint64_t>();
  result.randomIterations = value["randomIterations"].cast<std::size_t>();
  result.beamWidth = value["beamWidth"].cast<std::size_t>();
  result.maxExpandedStates = value["maxExpandedStates"].cast<std::size_t>();
  result.timeoutMs = value["timeoutMs"].cast<std::uint64_t>();
  result.modelId = value["modelId"].cast<std::string>();
  result.modelSha256 = value["modelSha256"].cast<std::string>();
  result.rollouts = value["rollouts"].cast<std::size_t>();
  result.selectionMode = value["selectionMode"].cast<std::string>();
  return result;
}

/// Загружает строгий grid_problem v1 и сообщает Python вызывающему точную причину отказа.
GridProblem parseProblem(const std::string & problemJson)
{
  GridProblemLoadResult loaded = loadGridProblemFromText(problemJson);
  if (!loaded.success)
    throw py::value_error(loaded.error);
  return std::move(loaded.problem);
}

/// Создаёт обучаемую среду из wire JSON с отдельно настраиваемыми лимитами v1.
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

/// Запускает M1 baseline и возвращает детерминированный wire JSON для построения датасета.
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
    throw py::value_error("unknown solver: " + solverName);
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

/// Проверяет пару problem/solution независимым M1-валидатором и возвращает ошибку или пустую строку.
// Оба JSON различаются публичными именами py::arg и проходят разные strict parsers.
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

/// Сравнивает два wire-решения общим C++ objective-компаратором.
bool isBetterSolution(const std::string & candidateJson, const std::string & referenceJson)
{
  const GridSolutionLoadResult candidate = loadGridSolutionFromText(candidateJson);
  if (!candidate.success)
    throw py::value_error("invalid candidate solution: " + candidate.error);
  const GridSolutionLoadResult reference = loadGridSolutionFromText(referenceJson);
  if (!reference.success)
    throw py::value_error("invalid reference solution: " + reference.error);
  if (candidate.solution.problemId != reference.solution.problemId)
    throw py::value_error("solutions belong to different problems");
  return isBetterGridSolution(candidate.solution, reference.solution);
}

/// Преобразует полигональное действие в аудируемый Python-словарь.
py::dict polygonActionToDict(const PolygonAction & action)
{
  py::dict result;
  result["partId"] = action.partId;
  result["instanceIndex"] = action.instanceIndex;
  result["xMicrometers"] = action.x;
  result["yMicrometers"] = action.y;
  result["rotationDegrees"] = action.rotationDegrees;
  return result;
}

/// Преобразует базовое полигональное наблюдение в read-only NumPy-массивы.
py::dict polygonObservationToDict(const PolygonObservation & observation)
{
  py::dict result;
  result["occupied"] = readonlyArray(observation.occupied, {observation.rasterRows, observation.rasterColumns});
  result["clearance"] = readonlyArray(observation.clearance, {observation.rasterRows, observation.rasterColumns});
  result["remaining"] = readonlyBoolArray(observation.remaining, {static_cast<py::ssize_t>(observation.remaining.size())});
  result["part_features"] = readonlyArray(observation.partFeatures, {static_cast<py::ssize_t>(observation.remaining.size()), 7});
  result["objective"] = readonlyArray(observation.objective, {7});
  return result;
}

/// Преобразует условное placement-наблюдение и его динамические действия.
py::dict polygonPlacementObservationToDict(const PolygonPlacementObservation & observation)
{
  py::dict result;
  result["raster"] = readonlyArray(observation.channels, {4, observation.rasterRows, observation.rasterColumns});
  py::list actions;
  for (const PolygonAction & action : observation.actions)
    actions.append(polygonActionToDict(action));
  result["actions"] = std::move(actions);
  result["candidate_features"] =
    readonlyArray(observation.candidateFeatures, {static_cast<py::ssize_t>(observation.actions.size()), 7});
  return result;
}

/// Загружает strict polygon_problem v1 для низкоуровневого Python API.
PolygonProblem parsePolygonProblem(const std::string & problemJson)
{
  PolygonProblemLoadResult loaded = loadPolygonProblemFromText(problemJson);
  if (!loaded.success)
    throw py::value_error(loaded.error);
  return std::move(loaded.problem);
}

/// Создаёт динамическую полигональную среду из wire JSON.
std::unique_ptr<PolygonLearningEnvironment> createPolygonEnvironment(const std::string & problemJson)
{
  PolygonProblem problem = parsePolygonProblem(problemJson);
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem, error);
  if (!environment)
    throw py::value_error(error);
  return environment;
}

/// Запускает полигональный baseline и возвращает воспроизводимый wire JSON.
std::string solvePolygonProblemJson(const std::string & problemJson, const std::string & solverName, std::uint64_t seed,
                                    std::size_t randomIterations, std::size_t beamWidth, std::size_t maxExpandedStates,
                                    std::uint64_t timeoutMs)
{
  const PolygonProblem problem = parsePolygonProblem(problemJson);
  SolverConfig config;
  if (!parseSolverKind(solverName, config.solver))
    throw py::value_error("unknown solver: " + solverName);
  config.seed = seed;
  config.randomIterations = randomIterations;
  config.beamWidth = beamWidth;
  config.maxExpandedStates = maxExpandedStates;
  config.timeoutMs = timeoutMs;
  PolygonSolution solution = solvePolygonProblem(problem, config);
  solution.metrics.candidateGenerationTimeUs = 0;
  solution.metrics.validationTimeUs = 0;
  solution.metrics.searchTimeUs = 0;
  solution.metrics.totalTimeUs = 0;
  return savePolygonSolutionToText(solution);
}

/// Проверяет strict problem/solution полигональным C++-валидатором.
std::string validatePolygonSolutionJson(const std::string & problemJson, const std::string & solutionJson)
{
  const PolygonProblem problem = parsePolygonProblem(problemJson);
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromText(solutionJson);
  if (!loaded.success)
    return loaded.error;
  const ValidationResult validation = validatePolygonSolution(problem, loaded.solution);
  return validation.success ? std::string{} : validation.error;
}

/// Сравнивает два polygon_solution общим C++ objective-порядком.
bool isBetterPolygonSolutionJson(const std::string & candidateJson, const std::string & referenceJson)
{
  const PolygonSolutionLoadResult candidate = loadPolygonSolutionFromText(candidateJson);
  const PolygonSolutionLoadResult reference = loadPolygonSolutionFromText(referenceJson);
  if (!candidate.success || !reference.success)
    throw py::value_error(candidate.success ? reference.error : candidate.error);
  if (candidate.solution.problemId != reference.solution.problemId)
    throw py::value_error("polygon solutions belong to different problems");
  return isBetterPolygonSolution(candidate.solution, reference.solution);
}
} // namespace

/// Регистрирует низкоуровневый модуль, оставляя удобный gym-like API Python-обёртке.
PYBIND11_MODULE(_aipackaging_solver, module)
{
  module.doc() = "Низкоуровневые bindings клеточной обучаемой среды AIPackaging";
  module.attr("__version__") = AIPACKAGING_PYTHON_VERSION;

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
          throw py::value_error("unknown incomplete solution status: " + incompleteStatus);
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
             throw py::value_error("action is absent from the stable catalog");
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

  py::class_<PolygonLearningEnvironment>(module, "PolygonLearningEnvironment")
    .def("reset", [](PolygonLearningEnvironment & environment) { return polygonObservationToDict(environment.reset()); })
    .def("observation",
         [](const PolygonLearningEnvironment & environment) { return polygonObservationToDict(environment.observation()); })
    .def("placement_observation", [](const PolygonLearningEnvironment & environment, std::size_t instance, int rotation)
         { return polygonPlacementObservationToDict(environment.placementObservation(instance, rotation)); })
    .def("actions",
         [](const PolygonLearningEnvironment & environment)
         {
           py::list result;
           for (const PolygonAction & action : environment.actions())
             result.append(polygonActionToDict(action));
           return result;
         })
    .def("step",
         [](PolygonLearningEnvironment & environment, std::size_t index)
         {
           const PolygonLearningStepResult step = environment.step(index);
           py::dict info;
           info["complete"] = step.complete;
           info["deadEnd"] = step.deadEnd;
           return py::make_tuple(polygonObservationToDict(step.observation), step.reward, step.terminated, false, info);
         })
    .def("snapshot_solution", [](const PolygonLearningEnvironment & environment, const py::dict & provenance)
         { return savePolygonSolutionToText(environment.solution(metadataFromDict(provenance))); })
    .def_property_readonly("is_complete", &PolygonLearningEnvironment::isComplete)
    .def_property_readonly("is_terminal", &PolygonLearningEnvironment::isTerminal)
    .def_property_readonly("problem_id", &PolygonLearningEnvironment::problemId);

  module.def("create_polygon_environment", &createPolygonEnvironment, py::arg("problem_json"));
  module.def("solve_polygon_problem", &solvePolygonProblemJson, py::arg("problem_json"), py::arg("solver") = "area-left-bottom",
             py::arg("seed") = 42, py::arg("random_iterations") = 64, py::arg("beam_width") = 32,
             py::arg("max_expanded_states") = 50000, py::arg("timeout_ms") = 0);
  module.def("validate_polygon_solution", &validatePolygonSolutionJson, py::arg("problem_json"), py::arg("solution_json"));
  module.def("is_better_polygon_solution", &isBetterPolygonSolutionJson, py::arg("candidate_json"), py::arg("reference_json"));
}
