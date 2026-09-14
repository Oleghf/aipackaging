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
      throw py::value_error("в полигональном действии отсутствует обязательное поле: " + field);
  if (value.size() != required.size())
    throw py::value_error("полигональное действие содержит неизвестные поля");
  return {value["partId"].cast<std::string>(), value["instanceIndex"].cast<std::uint32_t>(),
          value["xMicrometers"].cast<std::int64_t>(), value["yMicrometers"].cast<std::int64_t>(),
          value["rotationDegrees"].cast<int>()};
}

/// Преобразует базовое полигональное наблюдение в доступные только для чтения массивы NumPy.
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

/// Преобразует неизменные маски ориентаций и признаки экземпляров в массивы NumPy.
py::dict polygonStaticObservationToDict(const PolygonStaticObservation & observation)
{
  const py::ssize_t instances = static_cast<py::ssize_t>(observation.orientationMask.size() / 4);
  py::dict result;
  result["part_masks"] =
    readonlyArray(observation.partMasks, {instances, 4, observation.shapeRasterRows, observation.shapeRasterColumns});
  result["orientation_mask"] = readonlyBoolArray(observation.orientationMask, {instances, 4});
  result["part_features"] = readonlyArray(observation.partFeatures, {instances, 7});
  result["instance_part_ids"] = observation.instancePartIds;
  result["instance_indices"] = readonlyArray(observation.instanceIndices, {instances});
  result["shape_raster_rows"] = observation.shapeRasterRows;
  result["shape_raster_columns"] = observation.shapeRasterColumns;
  return result;
}

/// Преобразует изменяемое наблюдение и маску допустимых пар в массивы NumPy.
py::dict polygonDynamicObservationToDict(const PolygonDynamicObservation & observation)
{
  const py::ssize_t instances = static_cast<py::ssize_t>(observation.remaining.size());
  py::dict result;
  result["occupied"] = readonlyArray(observation.occupied, {observation.rasterRows, observation.rasterColumns});
  result["clearance"] = readonlyArray(observation.clearance, {observation.rasterRows, observation.rasterColumns});
  result["remaining"] = readonlyBoolArray(observation.remaining, {instances});
  result["pair_mask"] = readonlyBoolArray(observation.pairMask, {instances, 4});
  result["objective"] = readonlyArray(observation.objective, {7});
  return result;
}

/// Преобразует условное наблюдение размещения и его динамические действия.
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

/// Загружает строгий `polygon_problem` v1 для низкоуровневого API Python.
PolygonProblem parsePolygonProblem(const std::string & problemJson)
{
  PolygonProblemLoadResult loaded = loadPolygonProblemFromText(problemJson);
  if (!loaded.success)
    throw py::value_error(loaded.error);
  return std::move(loaded.problem);
}

/// Создаёт динамическую полигональную среду из JSON формата обмена.
std::unique_ptr<PolygonLearningEnvironment> createPolygonEnvironment(const std::string & problemJson, int rewardVersion)
{
  PolygonProblem problem = parsePolygonProblem(problemJson);
  PolygonLearningConfig config;
  config.rewardVersion = rewardVersion;
  std::string error;
  std::unique_ptr<PolygonLearningEnvironment> environment = PolygonLearningEnvironment::Create(problem, config, error);
  if (!environment)
    throw py::value_error(error);
  return environment;
}

/// Формирует расширенную диагностику компактного перехода для обучения политики.
py::dict compactStepInfo(const PolygonLearningCompactStepResult & step)
{
  py::dict info;
  info["complete"] = step.complete;
  info["deadEnd"] = step.deadEnd;
  info["rewardVersion"] = step.rewardVersion;
  info["potentialBefore"] = step.potentialBefore;
  info["potentialAfter"] = step.potentialAfter;
  info["componentDeltas"] = step.componentDeltas;
  return info;
}

/// Запускает полигональный базовый алгоритм и возвращает воспроизводимый JSON.
std::string solvePolygonProblemJson(const std::string & problemJson, const std::string & solverName, std::uint64_t seed,
                                    std::size_t randomIterations, std::size_t beamWidth, std::size_t maxExpandedStates,
                                    std::uint64_t timeoutMs)
{
  const PolygonProblem problem = parsePolygonProblem(problemJson);
  SolverConfig config;
  if (!parseSolverKind(solverName, config.solver))
    throw py::value_error("неизвестный решатель: " + solverName);
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

/// Проверяет строгую задачу или решение полигональным валидатором C++.
std::string validatePolygonSolutionJson(const std::string & problemJson, const std::string & solutionJson)
{
  const PolygonProblem problem = parsePolygonProblem(problemJson);
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromText(solutionJson);
  if (!loaded.success)
    return loaded.error;
  const ValidationResult validation = validatePolygonSolution(problem, loaded.solution);
  return validation.success ? std::string{} : validation.error;
}

/// Сравнивает два полигональных решения `polygon_solution` общим порядком целевой функции C++.
bool isBetterPolygonSolutionJson(const std::string & candidateJson, const std::string & referenceJson)
{
  const PolygonSolutionLoadResult candidate = loadPolygonSolutionFromText(candidateJson);
  const PolygonSolutionLoadResult reference = loadPolygonSolutionFromText(referenceJson);
  if (!candidate.success || !reference.success)
    throw py::value_error(candidate.success ? reference.error : candidate.error);
  if (candidate.solution.problemId != reference.solution.problemId)
    throw py::value_error("полигональные решения относятся к разным задачам");
  return isBetterPolygonSolution(candidate.solution, reference.solution);
}

/// Последовательно применяет скрытые размещения и принимает только полную точную раскладку.
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
      throw py::value_error("скрытое полигональное размещение должно быть объектом");
    const PolygonAction action = polygonActionFromDict(py::reinterpret_borrow<py::dict>(value));
    // Применение через среду повторяет рабочие проверки экземпляра, поворота,
    // границ, отступа от листа, зазора между деталями и положительного пересечения.
    if (!environment->apply(state, action))
      throw py::value_error("скрытое полигональное размещение некорректно по индексу " + std::to_string(index));
    ++index;
  }
  if (state.placements.size() != environment->instances().size())
    throw py::value_error("скрытая полигональная раскладка неполна");

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

/// Регистрирует публичный полигональный API без изменения имён и значений по умолчанию.
void bindPolygon(py::module_ & module)
{
  py::class_<PolygonLearningEnvironment>(module, "PolygonLearningEnvironment")
    .def("reset", [](PolygonLearningEnvironment & environment) { return polygonObservationToDict(environment.reset()); })
    .def("observation",
         [](const PolygonLearningEnvironment & environment) { return polygonObservationToDict(environment.observation()); })
    .def("static_observation", [](const PolygonLearningEnvironment & environment)
         { return polygonStaticObservationToDict(environment.staticObservation()); })
    .def("dynamic_observation", [](const PolygonLearningEnvironment & environment)
         { return polygonDynamicObservationToDict(environment.dynamicObservation()); })
    .def("reset_compact",
         [](PolygonLearningEnvironment & environment) { return polygonDynamicObservationToDict(environment.resetCompact()); })
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
    .def("step_compact",
         [](PolygonLearningEnvironment & environment, std::size_t index)
         {
           const PolygonLearningCompactStepResult step = environment.stepCompact(index);
           return py::make_tuple(polygonDynamicObservationToDict(step.observation), step.reward, step.terminated, false,
                                 compactStepInfo(step));
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

  module.def("create_polygon_environment", &createPolygonEnvironment, py::arg("problem_json"), py::arg("reward_version") = 1);
  module.def("solve_polygon_problem", &solvePolygonProblemJson, py::arg("problem_json"), py::arg("solver") = "area-left-bottom",
             py::arg("seed") = 42, py::arg("random_iterations") = 64, py::arg("beam_width") = 32,
             py::arg("max_expanded_states") = 50000, py::arg("timeout_ms") = 0);
  module.def("validate_polygon_solution", &validatePolygonSolutionJson, py::arg("problem_json"), py::arg("solution_json"));
  module.def("is_better_polygon_solution", &isBetterPolygonSolutionJson, py::arg("candidate_json"), py::arg("reference_json"));
  module.def("validate_hidden_polygon_layout", &validateHiddenPolygonLayout, py::arg("problem_json"), py::arg("placements"));
}
} // namespace aipackaging::python
