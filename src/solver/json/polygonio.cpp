#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <string_view>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <nlohmann/json.hpp>

#include "atomicfile.h"

namespace aipackaging::solver
{
namespace
{
using Json = nlohmann::json;

/// Проверяет объект на точное множество разрешённых полей.
bool onlyKeys(const Json & value, std::initializer_list<std::string_view> keys)
{
  if (!value.is_object())
    return false;
  for (const auto & item : value.items())
    if (std::none_of(keys.begin(), keys.end(), [&item](std::string_view key) { return item.key() == key; }))
      return false;
  return true;
}

/// Читает обязательную строку без JSON-преобразований.
bool readString(const Json & object, const char * key, std::string & value)
{
  if (!object.contains(key) || !object.at(key).is_string())
    return false;
  value = object.at(key).get<std::string>();
  return true;
}

/// Читает обязательное конечное число.
bool readDouble(const Json & object, const char * key, double & value)
{
  if (!object.contains(key) || !object.at(key).is_number())
    return false;
  value = object.at(key).get<double>();
  return std::isfinite(value);
}

/// Читает обязательное целое в диапазоне типа `int`.
bool readInt(const Json & object, const char * key, int & value)
{
  if (!object.contains(key) || !object.at(key).is_number_integer())
    return false;
  try
  {
    const std::int64_t parsed = object.at(key).get<std::int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
      return false;
    value = static_cast<int>(parsed);
    return true;
  }
  catch (const Json::exception &)
  {
    return false;
  }
}

/// Читает обязательное беззнаковое целое в диапазоне типа `uint64_t`.
bool readUint64(const Json & object, const char * key, std::uint64_t & value)
{
  if (!object.contains(key) || !object.at(key).is_number_integer())
    return false;
  try
  {
    if (object.at(key).is_number_unsigned())
    {
      value = object.at(key).get<std::uint64_t>();
      return true;
    }
    const std::int64_t parsed = object.at(key).get<std::int64_t>();
    if (parsed < 0)
      return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
  }
  catch (const Json::exception &)
  {
    return false;
  }
}

/// Читает размер контейнера без потери диапазона текущей платформы.
bool readSize(const Json & object, const char * key, std::size_t & value)
{
  std::uint64_t parsed = 0;
  if (!readUint64(object, key, parsed) || parsed > std::numeric_limits<std::size_t>::max())
    return false;
  value = static_cast<std::size_t>(parsed);
  return true;
}

/// Проверяет каноническую запись SHA-256 в нижнем регистре.
bool validSha256(const std::string & value)
{
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character)
                                           { return std::isdigit(character) || (character >= 'a' && character <= 'f'); });
}

/// Читает обязательную знаковую микронную координату.
bool readInt64(const Json & object, const char * key, std::int64_t & value)
{
  if (!object.contains(key) || !object.at(key).is_number_integer())
    return false;
  try
  {
    value = object.at(key).get<std::int64_t>();
    return true;
  }
  catch (const Json::exception &)
  {
    return false;
  }
}

/// Читает точку миллиметрового исходного пути.
bool readPoint(const Json & value, PolygonPointMm & point)
{
  return onlyKeys(value, {"x", "y"}) && readDouble(value, "x", point.x) && readDouble(value, "y", point.y);
}

/// Разбирает один замкнутый путь и строго проверяет поля каждого вида сегмента.
bool readPath(const Json & value, PolygonPath & path, std::string & error)
{
  if (!onlyKeys(value, {"start", "segments"}) || !value.contains("start") || !readPoint(value.at("start"), path.start) ||
      !value.contains("segments") || !value.at("segments").is_array())
  {
    error = "invalid polygon path";
    return false;
  }
  for (const Json & item : value.at("segments"))
  {
    std::string type;
    PolygonSegment segment;
    if (!readString(item, "type", type))
    {
      error = "polygon segment requires type";
      return false;
    }
    if (type == "line")
    {
      if (!onlyKeys(item, {"type", "end"}) || !item.contains("end") || !readPoint(item.at("end"), segment.end))
      {
        error = "invalid line segment";
        return false;
      }
      segment.kind = PolygonSegmentKind::Line;
    }
    else if (type == "arc")
    {
      if (!onlyKeys(item, {"type", "end", "center", "clockwise"}) || !item.contains("end") ||
          !readPoint(item.at("end"), segment.end) || !item.contains("center") || !readPoint(item.at("center"), segment.center) ||
          !item.contains("clockwise") || !item.at("clockwise").is_boolean())
      {
        error = "invalid arc segment";
        return false;
      }
      segment.kind = PolygonSegmentKind::Arc;
      segment.clockwise = item.at("clockwise").get<bool>();
    }
    else if (type == "cubic_bezier")
    {
      if (!onlyKeys(item, {"type", "end", "control1", "control2"}) || !item.contains("end") ||
          !readPoint(item.at("end"), segment.end) || !item.contains("control1") ||
          !readPoint(item.at("control1"), segment.control1) || !item.contains("control2") ||
          !readPoint(item.at("control2"), segment.control2))
      {
        error = "invalid cubic Bezier segment";
        return false;
      }
      segment.kind = PolygonSegmentKind::CubicBezier;
    }
    else
    {
      error = "unsupported polygon segment type";
      return false;
    }
    path.segments.push_back(segment);
  }
  return true;
}

/// Преобразует точку исходного пути в JSON.
Json pointJson(const PolygonPointMm & point)
{
  return {{"x", point.x}, {"y", point.y}};
}

/// Преобразует путь с вариантными сегментами в JSON.
Json pathJson(const PolygonPath & path)
{
  Json segments = Json::array();
  for (const PolygonSegment & segment : path.segments)
  {
    if (segment.kind == PolygonSegmentKind::Line)
      segments.push_back({{"type", "line"}, {"end", pointJson(segment.end)}});
    else if (segment.kind == PolygonSegmentKind::Arc)
      segments.push_back({{"type", "arc"},
                          {"end", pointJson(segment.end)},
                          {"center", pointJson(segment.center)},
                          {"clockwise", segment.clockwise}});
    else
      segments.push_back({{"type", "cubic_bezier"},
                          {"end", pointJson(segment.end)},
                          {"control1", pointJson(segment.control1)},
                          {"control2", pointJson(segment.control2)}});
  }
  return {{"start", pointJson(path.start)}, {"segments", std::move(segments)}};
}

/// Создаёт единообразную ошибку загрузки задачи.
PolygonProblemLoadResult problemFailure(const std::string & error)
{
  return {false, {}, error};
}

/// Создаёт единообразную ошибку загрузки решения.
PolygonSolutionLoadResult solutionFailure(const std::string & error)
{
  return {false, {}, error};
}

/// Читает метаданные решателя общего формата базовых алгоритмов.
bool readSolver(const Json & value, SolverMetadata & solver)
{
  if (!onlyKeys(value, {"name", "projectVersion", "revision", "seed", "randomIterations", "beamWidth", "maxExpandedStates",
                        "timeoutMs"}) ||
      !readString(value, "name", solver.name) || !readString(value, "projectVersion", solver.projectVersion) ||
      !readString(value, "revision", solver.revision) || !readUint64(value, "seed", solver.seed) ||
      !readUint64(value, "timeoutMs", solver.timeoutMs))
    return false;
  std::uint64_t randomIterations = 0;
  std::uint64_t beamWidth = 0;
  std::uint64_t maxExpanded = 0;
  if (!readUint64(value, "randomIterations", randomIterations) || !readUint64(value, "beamWidth", beamWidth) ||
      !readUint64(value, "maxExpandedStates", maxExpanded) || randomIterations > std::numeric_limits<std::size_t>::max() ||
      beamWidth > std::numeric_limits<std::size_t>::max() || maxExpanded > std::numeric_limits<std::size_t>::max())
    return false;
  solver.family = SolverFamily::Baseline;
  solver.randomIterations = static_cast<std::size_t>(randomIterations);
  solver.beamWidth = static_cast<std::size_t>(beamWidth);
  solver.maxExpandedStates = static_cast<std::size_t>(maxExpanded);
  return true;
}

/// Читает происхождение нейросетевого или гибридного решения полигонального формата v2.
bool readSolverV2(const Json & value, SolverMetadata & solver)
{
  if (!onlyKeys(value, {"family", "name", "projectVersion", "revision", "seed", "baseline", "policy"}))
    return false;
  std::string family;
  if (!readString(value, "family", family) || !parseSolverFamily(family, solver.family) ||
      !readString(value, "name", solver.name) || solver.name.empty() ||
      !readString(value, "projectVersion", solver.projectVersion) || !readString(value, "revision", solver.revision) ||
      !readUint64(value, "seed", solver.seed))
    return false;
  const bool needsBaseline = solver.family != SolverFamily::Neural;
  const bool needsPolicy = solver.family != SolverFamily::Baseline;
  if (!value.contains("baseline") || !value.contains("policy") ||
      (needsBaseline ? !value.at("baseline").is_object() : !value.at("baseline").is_null()) ||
      (needsPolicy ? !value.at("policy").is_object() : !value.at("policy").is_null()))
    return false;
  if (needsBaseline)
  {
    const Json & baseline = value.at("baseline");
    if (!onlyKeys(baseline, {"randomIterations", "beamWidth", "maxExpandedStates", "timeoutMs"}) ||
        !readSize(baseline, "randomIterations", solver.randomIterations) || !readSize(baseline, "beamWidth", solver.beamWidth) ||
        !readSize(baseline, "maxExpandedStates", solver.maxExpandedStates) ||
        !readUint64(baseline, "timeoutMs", solver.timeoutMs))
      return false;
  }
  if (needsPolicy)
  {
    const Json & policy = value.at("policy");
    if (!onlyKeys(policy, {"modelId", "modelSha256", "rollouts", "selectionMode"}) ||
        !readString(policy, "modelId", solver.modelId) || solver.modelId.empty() ||
        !readString(policy, "modelSha256", solver.modelSha256) || !validSha256(solver.modelSha256) ||
        !readSize(policy, "rollouts", solver.rollouts) || solver.rollouts == 0 ||
        !readString(policy, "selectionMode", solver.selectionMode) ||
        (solver.selectionMode != "greedy" && solver.selectionMode != "sampled-best-of" &&
         solver.selectionMode != "hybrid-best-of"))
      return false;
    if ((solver.family == SolverFamily::Hybrid) != (solver.selectionMode == "hybrid-best-of"))
      return false;
  }
  return true;
}
} // namespace

/// Читает строгий `polygon_problem` v1 и запускает геометрическую нормализацию.
PolygonProblemLoadResult loadPolygonProblemFromText(const std::string & text)
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
  // Унаследованная клеточная сцена не является полигональной задачей и не преобразуется без масштаба клетки.
  if (root.is_object() && root.value("format", Json()) == "aipackaging.packing_scene")
    return problemFailure("Формат `aipackaging.packing_scene` больше не поддерживается; откройте `polygon_problem` v1");
  if (!onlyKeys(root, {"format", "version", "problemId", "sheet", "manufacturing", "parts", "objective"}))
    return problemFailure("polygon problem contains unknown fields or is not an object");
  PolygonProblem problem;
  std::string format;
  int version = 0;
  if (!readString(root, "format", format) || format != "aipackaging.polygon_problem" || !readInt(root, "version", version) ||
      version != 1 || !readString(root, "problemId", problem.problemId))
    return problemFailure("unsupported polygon problem format or version");
  if (!root.contains("sheet") || !onlyKeys(root.at("sheet"), {"width", "height", "unit"}) ||
      !readDouble(root.at("sheet"), "width", problem.sheet.width) ||
      !readDouble(root.at("sheet"), "height", problem.sheet.height) || !readString(root.at("sheet"), "unit", problem.sheet.unit))
    return problemFailure("invalid polygon sheet");
  if (!root.contains("manufacturing") ||
      !onlyKeys(root.at("manufacturing"), {"sheetMargin", "partSpacing", "kerf", "curveTolerance"}) ||
      !readDouble(root.at("manufacturing"), "sheetMargin", problem.manufacturing.sheetMargin) ||
      !readDouble(root.at("manufacturing"), "partSpacing", problem.manufacturing.partSpacing) ||
      !readDouble(root.at("manufacturing"), "kerf", problem.manufacturing.kerf) ||
      !readDouble(root.at("manufacturing"), "curveTolerance", problem.manufacturing.curveTolerance))
    return problemFailure("invalid manufacturing parameters");
  if (!root.contains("parts") || !root.at("parts").is_array())
    return problemFailure("parts must be an array");
  for (const Json & item : root.at("parts"))
  {
    if (!onlyKeys(item, {"id", "quantity", "outer", "holes", "allowedRotations"}))
      return problemFailure("polygon part contains unknown fields");
    PolygonPart part;
    std::uint64_t quantity = 0;
    std::string error;
    if (!readString(item, "id", part.id) || !readUint64(item, "quantity", quantity) ||
        quantity > std::numeric_limits<std::uint32_t>::max() || !item.contains("outer") ||
        !readPath(item.at("outer"), part.outer, error) || !item.contains("holes") || !item.at("holes").is_array() ||
        !item.contains("allowedRotations") || !item.at("allowedRotations").is_array())
      return problemFailure(error.empty() ? "invalid polygon part" : error);
    part.quantity = static_cast<std::uint32_t>(quantity);
    for (const Json & hole : item.at("holes"))
    {
      PolygonPath path;
      if (!readPath(hole, path, error))
        return problemFailure(error);
      part.holes.push_back(std::move(path));
    }
    for (const Json & rotation : item.at("allowedRotations"))
    {
      int parsedRotation = 0;
      const Json wrapper = {{"rotation", rotation}};
      if (!readInt(wrapper, "rotation", parsedRotation))
        return problemFailure("allowedRotations must contain integers");
      part.allowedRotations.push_back(parsedRotation);
    }
    problem.parts.push_back(std::move(part));
  }
  if (!root.contains("objective") || !onlyKeys(root.at("objective"), {"type", "version"}) ||
      !readString(root.at("objective"), "type", problem.objective.type) ||
      !readInt(root.at("objective"), "version", problem.objective.version))
    return problemFailure("invalid polygon objective");
  std::string normalizationError;
  if (!PolygonEnvironment::Create(problem, normalizationError))
    return problemFailure(normalizationError);
  return {true, std::move(problem), {}};
}

/// Читает файл целиком и передаёт текст строгому синтаксическому анализатору.
PolygonProblemLoadResult loadPolygonProblemFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input)
    return problemFailure("unable to open polygon problem file");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadPolygonProblemFromText(buffer.str());
}

/// Строит стабильное дерево JSON исходной задачи без раскрытия nlohmann/json в API.
std::string savePolygonProblemToText(const PolygonProblem & problem)
{
  Json parts = Json::array();
  for (const PolygonPart & part : problem.parts)
  {
    Json holes = Json::array();
    for (const PolygonPath & hole : part.holes)
      holes.push_back(pathJson(hole));
    parts.push_back({{"id", part.id},
                     {"quantity", part.quantity},
                     {"outer", pathJson(part.outer)},
                     {"holes", std::move(holes)},
                     {"allowedRotations", part.allowedRotations}});
  }
  Json root = {{"format", "aipackaging.polygon_problem"},
               {"version", 1},
               {"problemId", problem.problemId},
               {"sheet", {{"width", problem.sheet.width}, {"height", problem.sheet.height}, {"unit", problem.sheet.unit}}},
               {"manufacturing",
                {{"sheetMargin", problem.manufacturing.sheetMargin},
                 {"partSpacing", problem.manufacturing.partSpacing},
                 {"kerf", problem.manufacturing.kerf},
                 {"curveTolerance", problem.manufacturing.curveTolerance}}},
               {"parts", std::move(parts)},
               {"objective", {{"type", problem.objective.type}, {"version", problem.objective.version}}}};
  return root.dump(2) + '\n';
}

/// Разбирает `polygon_solution` v1/v2 без доверия к записанной целевой функции.
PolygonSolutionLoadResult loadPolygonSolutionFromText(const std::string & text)
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
    return solutionFailure("polygon solution contains unknown fields");
  PolygonSolution solution;
  std::string format;
  std::string status;
  int version = 0;
  if (!readString(root, "format", format) || format != "aipackaging.polygon_solution" || !readInt(root, "version", version) ||
      (version != 1 && version != 2) || !readString(root, "problemId", solution.problemId) ||
      !readString(root, "status", status) || !parseSolveStatus(status, solution.status) ||
      !readString(root, "errorMessage", solution.errorMessage))
    return solutionFailure("unsupported polygon solution format, version, or status");
  solution.wireVersion = version;
  if (!root.contains("placements") || !root.at("placements").is_array())
    return solutionFailure("placements must be an array");
  for (const Json & item : root.at("placements"))
  {
    if (!onlyKeys(item, {"partId", "instanceIndex", "xMicrometers", "yMicrometers", "rotationDegrees"}))
      return solutionFailure("invalid polygon placement fields");
    PolygonPlacement placement;
    std::uint64_t instanceIndex = 0;
    if (!readString(item, "partId", placement.partId) || !readUint64(item, "instanceIndex", instanceIndex) ||
        instanceIndex > std::numeric_limits<std::uint32_t>::max() || !readInt64(item, "xMicrometers", placement.x) ||
        !readInt64(item, "yMicrometers", placement.y) || !readInt(item, "rotationDegrees", placement.rotationDegrees))
      return solutionFailure("invalid polygon placement");
    placement.instanceIndex = static_cast<std::uint32_t>(instanceIndex);
    solution.placements.push_back(std::move(placement));
  }
  if (!root.contains("objective") ||
      !onlyKeys(root.at("objective"),
                {"usedLengthMicrometers", "primaryRemnantWidthMicrometers", "largestExtraRectangleSquareMicrometers",
                 "fragmentationPenaltySquareMicrometers", "placedParts", "totalParts", "placedAreaSquareMicrometers",
                 "totalPartAreaSquareMicrometers", "materialUtilization", "rasterColumns", "rasterRows"}))
    return solutionFailure("invalid polygon objective fields");
  Json objective = root.at("objective");
  std::uint64_t placedParts = 0;
  std::uint64_t totalParts = 0;
  if (!readInt64(objective, "usedLengthMicrometers", solution.objective.usedLength) ||
      !readInt64(objective, "primaryRemnantWidthMicrometers", solution.objective.primaryRemnantWidth) ||
      !readUint64(objective, "largestExtraRectangleSquareMicrometers", solution.objective.largestExtraRectangleArea) ||
      !readUint64(objective, "fragmentationPenaltySquareMicrometers", solution.objective.fragmentationPenalty) ||
      !readUint64(objective, "placedParts", placedParts) || !readUint64(objective, "totalParts", totalParts) ||
      !readUint64(objective, "placedAreaSquareMicrometers", solution.objective.placedArea) ||
      !readUint64(objective, "totalPartAreaSquareMicrometers", solution.objective.totalPartArea) ||
      !readDouble(objective, "materialUtilization", solution.objective.materialUtilization) ||
      !readInt(objective, "rasterColumns", solution.objective.rasterColumns) ||
      !readInt(objective, "rasterRows", solution.objective.rasterRows))
    return solutionFailure("invalid polygon objective values");
  if (placedParts > std::numeric_limits<std::size_t>::max() || totalParts > std::numeric_limits<std::size_t>::max() ||
      solution.objective.materialUtilization < 0.0 || solution.objective.materialUtilization > 1.0 ||
      solution.objective.rasterColumns != 128 || solution.objective.rasterRows != 128)
    return solutionFailure("polygon objective values are outside contract limits");
  solution.objective.placedParts = static_cast<std::size_t>(placedParts);
  solution.objective.totalParts = static_cast<std::size_t>(totalParts);
  if (!root.contains("metrics") ||
      !onlyKeys(root.at("metrics"), {"candidatesGenerated", "candidatesValidated", "expandedStates", "candidateGenerationTimeUs",
                                     "validationTimeUs", "searchTimeUs", "totalTimeUs"}))
    return solutionFailure("invalid polygon metrics fields");
  Json metrics = root.at("metrics");
  if (!readUint64(metrics, "candidatesGenerated", solution.metrics.candidatesGenerated) ||
      !readUint64(metrics, "candidatesValidated", solution.metrics.candidatesValidated) ||
      !readUint64(metrics, "expandedStates", solution.metrics.expandedStates) ||
      !readUint64(metrics, "candidateGenerationTimeUs", solution.metrics.candidateGenerationTimeUs) ||
      !readUint64(metrics, "validationTimeUs", solution.metrics.validationTimeUs) ||
      !readUint64(metrics, "searchTimeUs", solution.metrics.searchTimeUs) ||
      !readUint64(metrics, "totalTimeUs", solution.metrics.totalTimeUs))
    return solutionFailure("invalid polygon metrics values");
  if (!root.contains("solver") ||
      (version == 1 ? !readSolver(root.at("solver"), solution.solver) : !readSolverV2(root.at("solver"), solution.solver)))
    return solutionFailure("invalid polygon solver metadata");
  return {true, std::move(solution), {}};
}

/// Читает файл решения целиком и делегирует обработку текстовому синтаксическому анализатору.
PolygonSolutionLoadResult loadPolygonSolutionFromFile(const std::string & filePath)
{
  std::ifstream input(filePath);
  if (!input)
    return solutionFailure("unable to open polygon solution file");
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return loadPolygonSolutionFromText(buffer.str());
}

/// Сериализует точные координаты, целевую функцию, метрики и происхождение результата.
std::string savePolygonSolutionToText(const PolygonSolution & solution)
{
  Json placements = Json::array();
  for (const PolygonPlacement & placement : solution.placements)
    placements.push_back({{"partId", placement.partId},
                          {"instanceIndex", placement.instanceIndex},
                          {"xMicrometers", placement.x},
                          {"yMicrometers", placement.y},
                          {"rotationDegrees", placement.rotationDegrees}});
  const auto & objective = solution.objective;
  const auto & metrics = solution.metrics;
  const auto & solver = solution.solver;
  Json root = {{"format", "aipackaging.polygon_solution"},
               {"version", solution.wireVersion},
               {"problemId", solution.problemId},
               {"status", toString(solution.status)},
               {"placements", std::move(placements)},
               {"objective",
                {{"usedLengthMicrometers", objective.usedLength},
                 {"primaryRemnantWidthMicrometers", objective.primaryRemnantWidth},
                 {"largestExtraRectangleSquareMicrometers", objective.largestExtraRectangleArea},
                 {"fragmentationPenaltySquareMicrometers", objective.fragmentationPenalty},
                 {"placedParts", objective.placedParts},
                 {"totalParts", objective.totalParts},
                 {"placedAreaSquareMicrometers", objective.placedArea},
                 {"totalPartAreaSquareMicrometers", objective.totalPartArea},
                 {"materialUtilization", objective.materialUtilization},
                 {"rasterColumns", objective.rasterColumns},
                 {"rasterRows", objective.rasterRows}}},
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
  }
  else
  {
    Json baseline = nullptr;
    if (solver.family != SolverFamily::Neural)
      baseline = {{"randomIterations", solver.randomIterations},
                  {"beamWidth", solver.beamWidth},
                  {"maxExpandedStates", solver.maxExpandedStates},
                  {"timeoutMs", solver.timeoutMs}};
    Json policy = nullptr;
    if (solver.family != SolverFamily::Baseline)
      policy = {{"modelId", solver.modelId},
                {"modelSha256", solver.modelSha256},
                {"rollouts", solver.rollouts},
                {"selectionMode", solver.selectionMode}};
    root["solver"] = {{"family", toString(solver.family)}, {"name", solver.name}, {"projectVersion", solver.projectVersion},
                      {"revision", solver.revision},       {"seed", solver.seed}, {"baseline", std::move(baseline)},
                      {"policy", std::move(policy)}};
  }
  return root.dump(2) + '\n';
}

/// Открывает выходной файл в двоичном режиме с очисткой и проверяет завершение записи.
bool savePolygonSolutionToFile(const std::string & filePath, const PolygonSolution & solution, std::string & error)
{
  return internal::writeFileAtomically(filePath, savePolygonSolutionToText(solution), error);
}

/// Разбирает только корневой объект JSON и возвращает строковое поле формата.
std::string detectJsonFormat(const std::string & text)
{
  try
  {
    const Json root = Json::parse(text);
    if (root.is_object() && root.contains("format") && root.at("format").is_string())
      return root.at("format").get<std::string>();
  }
  catch (const Json::exception &)
  {
    // Для определения формата синтаксическая ошибка эквивалентна отсутствию
    // распознаваемого discriminator; подробную диагностику вернёт parser.
  }
  return {};
}
} // namespace aipackaging::solver
