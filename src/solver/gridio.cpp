#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string_view>

#include <gridenvironment.h>
#include <gridio.h>
#include <nlohmann/json.hpp>

namespace aipackaging::solver
{
namespace
{
using Json = nlohmann::json;

/// Проверяет объект на отсутствие полей, не объявленных контрактом текущей версии.
bool onlyKeys(const Json & value, std::initializer_list<std::string_view> keys)
{
  if (!value.is_object())
    return false;
  for (const auto & item : value.items())
  {
    const bool known = std::any_of(keys.begin(), keys.end(), [&item](std::string_view key) { return item.key() == key; });
    if (!known)
      return false;
  }
  return true;
}

/// Проверяет каноническую 64-символьную hex-запись SHA-256 из provenance модели.
bool validSha256(const std::string & value)
{
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isxdigit(character) != 0; });
}

/// Читает обязательное строковое поле без неявных преобразований типов.
bool readString(const Json & object, const char * key, std::string & result)
{
  if (!object.contains(key) || !object.at(key).is_string())
    return false;
  result = object.at(key).get<std::string>();
  return true;
}

/// Читает обязательное целое поле после проверки диапазона int.
bool readInt(const Json & object, const char * key, int & result)
{
  if (!object.contains(key) || !object.at(key).is_number_integer())
    return false;
  try
  {
    const std::int64_t value = object.at(key).get<std::int64_t>();
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
      return false;
    result = static_cast<int>(value);
    return true;
  }
  catch (const Json::exception &)
  {
    return false;
  }
}

/// Читает обязательное неотрицательное целое поле после проверки диапазона size_t.
bool readSize(const Json & object, const char * key, std::size_t & result)
{
  if (!object.contains(key) || !object.at(key).is_number_integer())
    return false;
  try
  {
    if (object.at(key).is_number_unsigned())
    {
      const std::uint64_t value = object.at(key).get<std::uint64_t>();
      if (value > std::numeric_limits<std::size_t>::max())
        return false;
      result = static_cast<std::size_t>(value);
      return true;
    }
    const std::int64_t value = object.at(key).get<std::int64_t>();
    if (value < 0 || static_cast<std::uint64_t>(value) > std::numeric_limits<std::size_t>::max())
      return false;
    result = static_cast<std::size_t>(value);
    return true;
  }
  catch (const Json::exception &)
  {
    return false;
  }
}

/// Читает обязательный беззнаковый 64-битный счётчик.
bool readUint64(const Json & object, const char * key, std::uint64_t & result)
{
  std::size_t value = 0;
  if (!readSize(object, key, value))
    return false;
  result = static_cast<std::uint64_t>(value);
  return true;
}

/// Создаёт единообразный ошибочный результат загрузки задачи.
GridProblemLoadResult problemFailure(const std::string & message)
{
  return {false, {}, message};
}

/// Создаёт единообразный ошибочный результат загрузки решения.
GridSolutionLoadResult solutionFailure(const std::string & message)
{
  return {false, {}, message};
}

/// Собирает каноническое JSON-дерево задачи из публичных value-типов.
Json problemJson(const GridProblem & problem)
{
  Json parts = Json::array();
  for (const GridPart & part : problem.parts)
  {
    Json cells = Json::array();
    for (const GridCell & cell : part.cells)
      cells.push_back({{"column", cell.column}, {"row", cell.row}});
    parts.push_back(
      {{"id", part.id}, {"quantity", part.quantity}, {"cells", std::move(cells)}, {"allowedRotations", part.allowedRotations}});
  }
  return {{"format", "aipackaging.grid_problem"},
          {"version", 1},
          {"problemId", problem.problemId},
          {"sheet", {{"columns", problem.sheet.columns}, {"rows", problem.sheet.rows}, {"unit", problem.sheet.unit}}},
          {"parts", std::move(parts)},
          {"objective", {{"type", problem.objective.type}, {"version", problem.objective.version}}}};
}

/// Собирает каноническое JSON-дерево решения со всеми метриками и metadata.
Json solutionJson(const GridSolution & solution)
{
  Json placements = Json::array();
  for (const GridPlacement & placement : solution.placements)
  {
    placements.push_back({{"partId", placement.partId},
                          {"instanceIndex", placement.instanceIndex},
                          {"column", placement.column},
                          {"row", placement.row},
                          {"rotationDegrees", placement.rotationDegrees}});
  }
  const ObjectiveComponents & objective = solution.objective;
  const SolverMetrics & metrics = solution.metrics;
  const SolverMetadata & solver = solution.solver;
  Json root = {{"format", "aipackaging.grid_solution"},
               {"version", solution.wireVersion},
               {"problemId", solution.problemId},
               {"status", toString(solution.status)},
               {"placements", std::move(placements)},
               {"objective",
                {{"usedLength", objective.usedLength},
                 {"primaryRemnantWidth", objective.primaryRemnantWidth},
                 {"largestExtraRectangleArea", objective.largestExtraRectangleArea},
                 {"fragmentationPenalty", objective.fragmentationPenalty},
                 {"placedParts", objective.placedParts},
                 {"totalParts", objective.totalParts},
                 {"placedCells", objective.placedCells},
                 {"totalPartCells", objective.totalPartCells},
                 {"materialUtilization", objective.materialUtilization}}},
               {"metrics",
                {{"candidatesGenerated", metrics.candidatesGenerated},
                 {"candidatesValidated", metrics.candidatesValidated},
                 {"expandedStates", metrics.expandedStates},
                 {"candidateGenerationTimeUs", metrics.candidateGenerationTimeUs},
                 {"validationTimeUs", metrics.validationTimeUs},
                 {"searchTimeUs", metrics.searchTimeUs},
                 {"totalTimeUs", metrics.totalTimeUs}}},
               {"errorMessage", solution.errorMessage}};

  if (solution.wireVersion == 1)
  {
    root["solver"] = {{"name", solver.name},
                      {"projectVersion", solver.projectVersion},
                      {"revision", solver.revision},
                      {"seed", solver.seed},
                      {"randomIterations", solver.randomIterations},
                      {"beamWidth", solver.beamWidth},
                      {"maxExpandedStates", solver.maxExpandedStates},
                      {"timeoutMs", solver.timeoutMs}};
    return root;
  }

  Json baseline = nullptr;
  if (solver.family != SolverFamily::Neural)
  {
    baseline = {{"randomIterations", solver.randomIterations},
                {"beamWidth", solver.beamWidth},
                {"maxExpandedStates", solver.maxExpandedStates},
                {"timeoutMs", solver.timeoutMs}};
  }
  Json policy = nullptr;
  if (solver.family != SolverFamily::Baseline)
  {
    policy = {{"modelId", solver.modelId},
              {"modelSha256", solver.modelSha256},
              {"rollouts", solver.rollouts},
              {"selectionMode", solver.selectionMode}};
  }
  root["solver"] = {{"family", toString(solver.family)}, {"name", solver.name}, {"projectVersion", solver.projectVersion},
                    {"revision", solver.revision},       {"seed", solver.seed}, {"baseline", std::move(baseline)},
                    {"policy", std::move(policy)}};
  return root;
}
} // namespace

/// Разбирает JSON, строго проверяет поля schema v1 и затем запускает доменную валидацию задачи.
GridProblemLoadResult loadGridProblemFromText(const std::string & text)
{
  Json root;
  try
  {
    root = Json::parse(text);
  }
  catch (const Json::exception & error)
  {
    return problemFailure(std::string("invalid JSON: ") + error.what());
  }
  if (!onlyKeys(root, {"format", "version", "problemId", "sheet", "parts", "objective"}))
    return problemFailure("grid problem contains unknown fields or is not an object");

  std::string format;
  int version = 0;
  GridProblem problem;
  if (!readString(root, "format", format) || format != "aipackaging.grid_problem" || !readInt(root, "version", version) ||
      version != 1 || !readString(root, "problemId", problem.problemId))
  {
    return problemFailure("unsupported grid problem format or version");
  }

  if (!root.contains("sheet") || !onlyKeys(root.at("sheet"), {"columns", "rows", "unit"}) ||
      !readInt(root.at("sheet"), "columns", problem.sheet.columns) || !readInt(root.at("sheet"), "rows", problem.sheet.rows) ||
      !readString(root.at("sheet"), "unit", problem.sheet.unit))
  {
    return problemFailure("invalid sheet");
  }
  if (!root.contains("parts") || !root.at("parts").is_array())
    return problemFailure("parts must be an array");

  // JSON-проверки не заменяют доменные: здесь контролируются типы и поля, а
  // связность, нормализация и отверстия проверяются после построения GridProblem.
  for (const Json & partJson : root.at("parts"))
  {
    if (!onlyKeys(partJson, {"id", "quantity", "cells", "allowedRotations"}))
      return problemFailure("part contains unknown fields or is not an object");
    GridPart part;
    std::size_t quantity = 0;
    if (!readString(partJson, "id", part.id) || !readSize(partJson, "quantity", quantity) ||
        quantity > std::numeric_limits<std::uint32_t>::max() || !partJson.contains("cells") || !partJson.at("cells").is_array() ||
        !partJson.contains("allowedRotations") || !partJson.at("allowedRotations").is_array())
    {
      return problemFailure("invalid part");
    }
    part.quantity = static_cast<std::uint32_t>(quantity);
    for (const Json & cellJson : partJson.at("cells"))
    {
      GridCell cell;
      if (!onlyKeys(cellJson, {"column", "row"}) || !readInt(cellJson, "column", cell.column) ||
          !readInt(cellJson, "row", cell.row))
      {
        return problemFailure("invalid part cell");
      }
      part.cells.push_back(cell);
    }
    for (const Json & rotationJson : partJson.at("allowedRotations"))
    {
      if (!rotationJson.is_number_integer())
        return problemFailure("allowedRotations must contain integers");
      try
      {
        part.allowedRotations.push_back(rotationJson.get<int>());
      }
      catch (const Json::exception &)
      {
        return problemFailure("rotation is outside integer range");
      }
    }
    problem.parts.push_back(std::move(part));
  }

  if (!root.contains("objective") || !onlyKeys(root.at("objective"), {"type", "version"}) ||
      !readString(root.at("objective"), "type", problem.objective.type) ||
      !readInt(root.at("objective"), "version", problem.objective.version))
  {
    return problemFailure("invalid objective");
  }
  const ValidationResult validation = validateGridProblem(problem);
  if (!validation.success)
    return problemFailure(validation.error);
  return {true, std::move(problem), {}};
}

/// Читает весь файл в память и делегирует строгому текстовому загрузчику.
GridProblemLoadResult loadGridProblemFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input.is_open())
    return problemFailure("unable to open grid problem file");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadGridProblemFromText(buffer.str());
}

/// Формирует стабильный отформатированный JSON с завершающим переводом строки.
std::string saveGridProblemToText(const GridProblem & problem)
{
  return problemJson(problem).dump(2) + '\n';
}

/// Разбирает grid_solution v1/v2 и проверяет его структуру без знания исходной задачи.
GridSolutionLoadResult loadGridSolutionFromText(const std::string & text)
{
  Json root;
  try
  {
    root = Json::parse(text);
  }
  catch (const Json::exception & error)
  {
    return solutionFailure(std::string("invalid JSON: ") + error.what());
  }
  if (!onlyKeys(root,
                {"format", "version", "problemId", "status", "placements", "objective", "metrics", "solver", "errorMessage"}))
  {
    return solutionFailure("grid solution contains unknown fields or is not an object");
  }

  GridSolution solution;
  std::string format;
  std::string status;
  int version = 0;
  if (!readString(root, "format", format) || format != "aipackaging.grid_solution" || !readInt(root, "version", version) ||
      (version != 1 && version != 2) || !readString(root, "problemId", solution.problemId) ||
      !readString(root, "status", status) || !parseSolveStatus(status, solution.status) ||
      !readString(root, "errorMessage", solution.errorMessage))
  {
    return solutionFailure("unsupported grid solution format, version, or status");
  }
  solution.wireVersion = version;

  if (!root.contains("placements") || !root.at("placements").is_array())
    return solutionFailure("placements must be an array");
  for (const Json & placementJson : root.at("placements"))
  {
    if (!onlyKeys(placementJson, {"partId", "instanceIndex", "column", "row", "rotationDegrees"}))
      return solutionFailure("invalid placement fields");
    GridPlacement placement;
    std::size_t instanceIndex = 0;
    if (!readString(placementJson, "partId", placement.partId) || !readSize(placementJson, "instanceIndex", instanceIndex) ||
        instanceIndex > std::numeric_limits<std::uint32_t>::max() || !readInt(placementJson, "column", placement.column) ||
        !readInt(placementJson, "row", placement.row) || !readInt(placementJson, "rotationDegrees", placement.rotationDegrees))
    {
      return solutionFailure("invalid placement");
    }
    placement.instanceIndex = static_cast<std::uint32_t>(instanceIndex);
    solution.placements.push_back(std::move(placement));
  }

  // Геометрическое соответствие objective исходной задаче намеренно проверяет
  // validateGridSolution: загрузчик отвечает только за wire-контракт.
  if (!root.contains("objective") ||
      !onlyKeys(root.at("objective"), {"usedLength", "primaryRemnantWidth", "largestExtraRectangleArea", "fragmentationPenalty",
                                       "placedParts", "totalParts", "placedCells", "totalPartCells", "materialUtilization"}))
  {
    return solutionFailure("invalid objective fields");
  }
  Json objective = root.at("objective");
  if (!readInt(objective, "usedLength", solution.objective.usedLength) ||
      !readInt(objective, "primaryRemnantWidth", solution.objective.primaryRemnantWidth) ||
      !readSize(objective, "largestExtraRectangleArea", solution.objective.largestExtraRectangleArea) ||
      !readSize(objective, "fragmentationPenalty", solution.objective.fragmentationPenalty) ||
      !readSize(objective, "placedParts", solution.objective.placedParts) ||
      !readSize(objective, "totalParts", solution.objective.totalParts) ||
      !readSize(objective, "placedCells", solution.objective.placedCells) ||
      !readSize(objective, "totalPartCells", solution.objective.totalPartCells) || !objective.contains("materialUtilization") ||
      !objective.at("materialUtilization").is_number())
  {
    return solutionFailure("invalid objective values");
  }
  solution.objective.materialUtilization = objective.at("materialUtilization").get<double>();
  if (!std::isfinite(solution.objective.materialUtilization) || solution.objective.materialUtilization < 0.0 ||
      solution.objective.materialUtilization > 1.0)
  {
    return solutionFailure("invalid materialUtilization");
  }

  if (!root.contains("metrics") ||
      !onlyKeys(root.at("metrics"), {"candidatesGenerated", "candidatesValidated", "expandedStates", "candidateGenerationTimeUs",
                                     "validationTimeUs", "searchTimeUs", "totalTimeUs"}))
  {
    return solutionFailure("invalid metrics fields");
  }
  Json metrics = root.at("metrics");
  if (!readUint64(metrics, "candidatesGenerated", solution.metrics.candidatesGenerated) ||
      !readUint64(metrics, "candidatesValidated", solution.metrics.candidatesValidated) ||
      !readUint64(metrics, "expandedStates", solution.metrics.expandedStates) ||
      !readUint64(metrics, "candidateGenerationTimeUs", solution.metrics.candidateGenerationTimeUs) ||
      !readUint64(metrics, "validationTimeUs", solution.metrics.validationTimeUs) ||
      !readUint64(metrics, "searchTimeUs", solution.metrics.searchTimeUs) ||
      !readUint64(metrics, "totalTimeUs", solution.metrics.totalTimeUs))
  {
    return solutionFailure("invalid metrics values");
  }

  if (!root.contains("solver"))
    return solutionFailure("invalid solver fields");
  Json solver = root.at("solver");

  if (version == 1)
  {
    if (!onlyKeys(solver, {"name", "projectVersion", "revision", "seed", "randomIterations", "beamWidth", "maxExpandedStates",
                           "timeoutMs"}))
      return solutionFailure("invalid solver fields");
    std::size_t randomIterations = 0;
    std::size_t beamWidth = 0;
    std::size_t maxExpandedStates = 0;
    if (!readString(solver, "name", solution.solver.name) ||
        !readString(solver, "projectVersion", solution.solver.projectVersion) ||
        !readString(solver, "revision", solution.solver.revision) || !readUint64(solver, "seed", solution.solver.seed) ||
        !readSize(solver, "randomIterations", randomIterations) || !readSize(solver, "beamWidth", beamWidth) ||
        !readSize(solver, "maxExpandedStates", maxExpandedStates) || !readUint64(solver, "timeoutMs", solution.solver.timeoutMs))
      return solutionFailure("invalid solver values");
    SolverKind solverKind;
    if (!parseSolverKind(solution.solver.name, solverKind))
      return solutionFailure("unknown solver name");
    solution.solver.family = SolverFamily::Baseline;
    solution.solver.randomIterations = randomIterations;
    solution.solver.beamWidth = beamWidth;
    solution.solver.maxExpandedStates = maxExpandedStates;
    return {true, std::move(solution), {}};
  }

  if (!onlyKeys(solver, {"family", "name", "projectVersion", "revision", "seed", "baseline", "policy"}))
    return solutionFailure("invalid solution v2 solver fields");
  std::string family;
  if (!readString(solver, "family", family) || !parseSolverFamily(family, solution.solver.family) ||
      !readString(solver, "name", solution.solver.name) || solution.solver.name.empty() ||
      !readString(solver, "projectVersion", solution.solver.projectVersion) ||
      !readString(solver, "revision", solution.solver.revision) || !readUint64(solver, "seed", solution.solver.seed))
    return solutionFailure("invalid solution v2 solver values");

  const bool requiresBaseline = solution.solver.family != SolverFamily::Neural;
  const bool requiresPolicy = solution.solver.family != SolverFamily::Baseline;
  if (!solver.contains("baseline") || (requiresBaseline != solver.at("baseline").is_object()) ||
      (!requiresBaseline && !solver.at("baseline").is_null()))
    return solutionFailure("solution v2 baseline provenance mismatch");
  if (!solver.contains("policy") || (requiresPolicy != solver.at("policy").is_object()) ||
      (!requiresPolicy && !solver.at("policy").is_null()))
    return solutionFailure("solution v2 policy provenance mismatch");

  if (requiresBaseline)
  {
    const Json & baseline = solver.at("baseline");
    if (!onlyKeys(baseline, {"randomIterations", "beamWidth", "maxExpandedStates", "timeoutMs"}) ||
        !readSize(baseline, "randomIterations", solution.solver.randomIterations) ||
        !readSize(baseline, "beamWidth", solution.solver.beamWidth) ||
        !readSize(baseline, "maxExpandedStates", solution.solver.maxExpandedStates) ||
        !readUint64(baseline, "timeoutMs", solution.solver.timeoutMs))
      return solutionFailure("invalid solution v2 baseline provenance");
  }
  if (requiresPolicy)
  {
    const Json & policy = solver.at("policy");
    if (!onlyKeys(policy, {"modelId", "modelSha256", "rollouts", "selectionMode"}) ||
        !readString(policy, "modelId", solution.solver.modelId) || solution.solver.modelId.empty() ||
        !readString(policy, "modelSha256", solution.solver.modelSha256) || !validSha256(solution.solver.modelSha256) ||
        !readSize(policy, "rollouts", solution.solver.rollouts) || solution.solver.rollouts == 0 ||
        !readString(policy, "selectionMode", solution.solver.selectionMode) ||
        (solution.solver.selectionMode != "greedy" && solution.solver.selectionMode != "sampled-best-of" &&
         solution.solver.selectionMode != "hybrid-best-of"))
      return solutionFailure("invalid solution v2 policy provenance");
    const bool hybridSelection = solution.solver.selectionMode == "hybrid-best-of";
    if ((solution.solver.family == SolverFamily::Hybrid) != hybridSelection)
      return solutionFailure("solution v2 family and policy selection mode mismatch");
  }
  return {true, std::move(solution), {}};
}

/// Читает весь файл решения и делегирует строгому текстовому загрузчику.
GridSolutionLoadResult loadGridSolutionFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input.is_open())
    return solutionFailure("unable to open grid solution file");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadGridSolutionFromText(buffer.str());
}

/// Формирует стабильный отформатированный JSON решения.
std::string saveGridSolutionToText(const GridSolution & solution)
{
  return solutionJson(solution).dump(2) + '\n';
}

/// Открывает файл, полностью записывает JSON и сообщает ошибки открытия или потока.
bool saveGridSolutionToFile(const std::string & filePath, const GridSolution & solution, std::string & error)
{
  std::ofstream output(filePath);
  if (!output.is_open())
  {
    error = "unable to open grid solution output file";
    return false;
  }
  output << saveGridSolutionToText(solution);
  if (!output.good())
  {
    error = "unable to write grid solution output file";
    return false;
  }
  error.clear();
  return true;
}
} // namespace aipackaging::solver
