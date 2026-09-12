#include <memory>
#include <string>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_learning.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <pybind11/stl.h>

#include "bindingsinternal.h"

namespace aipackaging::python
{
using namespace solver;

namespace
{
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

/// Строго преобразует Python-словарь скрытого размещения в полигональное действие.
PolygonAction polygonActionFromDict(const py::dict & value)
{
  static const std::vector<std::string> required = {"partId", "instanceIndex", "xMicrometers", "yMicrometers", "rotationDegrees"};
  for (const std::string & field : required)
    if (!value.contains(py::str(field)))
      throw py::value_error("polygon action is missing required field: " + field);
  if (value.size() != required.size())
    throw py::value_error("polygon action contains unknown fields");
  return {value["partId"].cast<std::string>(), value["instanceIndex"].cast<std::uint32_t>(),
          value["xMicrometers"].cast<std::int64_t>(), value["yMicrometers"].cast<std::int64_t>(),
          value["rotationDegrees"].cast<int>()};
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

/// Последовательно применяет скрытые placements и принимает только полную точную раскладку.
py::dict validateHiddenPolygonLayout(const std::string & problemJson, const py::list & placements)
{
  const PolygonProblem problem = parsePolygonProblem(problemJson);
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(problem, error);
  if (!environment)
    throw py::value_error(error);

  PolygonState state = environment->initialState();
  std::size_t index = 0;
  for (const py::handle value : placements)
  {
    if (!py::isinstance<py::dict>(value))
      throw py::value_error("hidden polygon placement must be an object");
    const PolygonAction action = polygonActionFromDict(py::reinterpret_borrow<py::dict>(value));
    // Применение через среду повторяет production-проверки экземпляра, поворота,
    // границ, margin, spacing и положительного пересечения.
    if (!environment->apply(state, action))
      throw py::value_error("hidden polygon placement is invalid at index " + std::to_string(index));
    ++index;
  }
  if (state.placements.size() != environment->instances().size())
    throw py::value_error("hidden polygon layout is incomplete");

  const PolygonObjectiveComponents objective = environment->evaluate(state);
  py::dict result;
  result["placedParts"] = objective.placedParts;
  result["totalParts"] = objective.totalParts;
  result["placedAreaSquareMicrometers"] = objective.placedArea;
  result["totalPartAreaSquareMicrometers"] = objective.totalPartArea;
  result["materialUtilization"] = objective.materialUtilization;
  return result;
}

} // namespace

/// Регистрирует публичный polygon API без изменения имён и значений по умолчанию.
void bindPolygon(py::module_ & module)
{
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
  module.def("validate_hidden_polygon_layout", &validateHiddenPolygonLayout, py::arg("problem_json"), py::arg("placements"));
}
} // namespace aipackaging::python
