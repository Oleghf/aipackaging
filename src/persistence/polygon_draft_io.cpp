#include <algorithm>
#include <array>
#include <limits>
#include <regex>
#include <stdexcept>

#include <aipackaging/editor/polygon_document_validation.h>
#include <aipackaging/editor/polygon_draft_io.h>

#include "atomicfile.h"
#include "strictjson.h"

namespace aipackaging::editor
{
namespace
{
using aipackaging::solver::internal::Json;
using aipackaging::solver::internal::StrictJsonResult;

constexpr std::string_view draftFormat = "aipackaging.polygon_draft";
constexpr int draftVersion = 1;

/// Преобразует структурированный отказ строгого чтения в русскую диагностику черновика.
bool accept(const StrictJsonResult & result, std::string_view context, std::string & error)
{
  if (result)
    return true;
  error = "Некорректный черновик: " + std::string(context);
  if (!result.detail.empty())
    error += " (" + result.detail + ")";
  return false;
}

/// Проверяет объект на точный набор полей и формирует контекстную диагностику.
bool keys(const Json & value, std::initializer_list<std::string_view> expected, std::string_view context, std::string & error)
{
  return accept(aipackaging::solver::internal::onlyKeys(value, expected), context, error);
}

/// Извлекает обязательный массив без неявных преобразований.
bool arrayField(const Json & object, const char * key, const Json *& value, std::string & error)
{
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_array())
  {
    error = "Некорректный черновик: поле `" + std::string(key) + "` должно быть массивом";
    return false;
  }
  value = &object.at(key);
  return true;
}

/// Извлекает обязательный объект без неявных преобразований.
bool objectField(const Json & object, const char * key, const Json *& value, std::string & error)
{
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_object())
  {
    error = "Некорректный черновик: поле `" + std::string(key) + "` должно быть объектом";
    return false;
  }
  value = &object.at(key);
  return true;
}

/// Извлекает обязательный логический признак без неявных преобразований.
bool boolField(const Json & object, const char * key, bool & value, std::string & error)
{
  if (!object.is_object() || !object.contains(key) || !object.at(key).is_boolean())
  {
    error = "Некорректный черновик: поле `" + std::string(key) + "` должно быть логическим";
    return false;
  }
  value = object.at(key).get<bool>();
  return true;
}

/// Читает ненулевой идентификатор сущности.
bool entityId(const Json & object, const char * key, EntityId & id, std::string & error)
{
  std::uint64_t value = 0;
  if (!accept(aipackaging::solver::internal::readUint64(object, key, value), "неверный идентификатор сущности", error))
    return false;
  if (value == 0)
  {
    error = "Некорректный черновик: идентификатор сущности должен быть ненулевым";
    return false;
  }
  id.value = value;
  return true;
}

/// Читает точку вместе с её устойчивым идентификатором.
bool readPoint(const Json & value, EditablePoint & point, std::string & error)
{
  if (!keys(value, {"entityId", "x", "y"}, "неверные поля точки", error) || !entityId(value, "entityId", point.id, error) ||
      !accept(aipackaging::solver::internal::readDouble(value, "x", point.x), "неверная координата `x`", error) ||
      !accept(aipackaging::solver::internal::readDouble(value, "y", point.y), "неверная координата `y`", error))
    return false;
  return true;
}

/// Читает сегмент и только те опорные точки, которые принадлежат его виду.
bool readSegment(const Json & value, EditableSegment & segment, std::string & error)
{
  std::string type;
  if (!value.is_object() ||
      !accept(aipackaging::solver::internal::readString(value, "type", type), "неверный вид сегмента", error))
    return false;
  if (type == "line")
  {
    segment.kind = EditableSegmentKind::Line;
    return keys(value, {"entityId", "type"}, "неверные поля отрезка", error) && entityId(value, "entityId", segment.id, error);
  }
  if (type == "arc")
  {
    const Json * center = nullptr;
    segment.kind = EditableSegmentKind::Arc;
    return keys(value, {"entityId", "type", "center", "clockwise"}, "неверные поля дуги", error) &&
           entityId(value, "entityId", segment.id, error) && objectField(value, "center", center, error) &&
           readPoint(*center, segment.center, error) && boolField(value, "clockwise", segment.clockwise, error);
  }
  if (type == "cubic_bezier")
  {
    const Json * control1 = nullptr;
    const Json * control2 = nullptr;
    segment.kind = EditableSegmentKind::CubicBezier;
    return keys(value, {"entityId", "type", "control1", "control2"}, "неверные поля кривой Bézier", error) &&
           entityId(value, "entityId", segment.id, error) && objectField(value, "control1", control1, error) &&
           objectField(value, "control2", control2, error) && readPoint(*control1, segment.control1, error) &&
           readPoint(*control2, segment.control2, error);
  }
  error = "Некорректный черновик: неизвестный вид сегмента `" + type + "`";
  return false;
}

/// Читает открытую либо замкнутую цепочку без требования геометрической корректности.
bool readPath(const Json & value, EditablePath & path, std::string & error)
{
  const Json * vertices = nullptr;
  const Json * segments = nullptr;
  if (!keys(value, {"entityId", "closed", "vertices", "segments"}, "неверные поля контура", error) ||
      !entityId(value, "entityId", path.id, error) || !boolField(value, "closed", path.closed, error) ||
      !arrayField(value, "vertices", vertices, error) || !arrayField(value, "segments", segments, error))
    return false;
  path.vertices.clear();
  path.segments.clear();
  for (const Json & item : *vertices)
  {
    EditablePoint point;
    if (!readPoint(item, point, error))
      return false;
    path.vertices.push_back(std::move(point));
  }
  for (const Json & item : *segments)
  {
    EditableSegment segment;
    if (!readSegment(item, segment, error))
      return false;
    path.segments.push_back(std::move(segment));
  }
  return true;
}

/// Читает один тип детали и допускает локально неполный внешний контур.
bool readPart(const Json & value, EditablePart & part, std::string & error)
{
  const Json * holes = nullptr;
  const Json * rotations = nullptr;
  std::uint64_t quantity = 0;
  if (!keys(value, {"entityId", "partId", "quantity", "outer", "holes", "allowedRotations"}, "неверные поля детали", error) ||
      !entityId(value, "entityId", part.id, error) ||
      !accept(aipackaging::solver::internal::readString(value, "partId", part.partId), "неверный идентификатор детали", error) ||
      !accept(aipackaging::solver::internal::readUint64(value, "quantity", quantity), "неверное количество деталей", error) ||
      quantity > std::numeric_limits<std::uint32_t>::max() || !arrayField(value, "holes", holes, error) ||
      !arrayField(value, "allowedRotations", rotations, error))
  {
    if (error.empty())
      error = "Некорректный черновик: количество деталей находится вне допустимого диапазона";
    return false;
  }
  part.quantity = static_cast<std::uint32_t>(quantity);
  if (!value.contains("outer"))
  {
    error = "Некорректный черновик: отсутствует поле `outer`";
    return false;
  }
  if (value.at("outer").is_null())
    part.outer.reset();
  else
  {
    EditablePath outer;
    if (!readPath(value.at("outer"), outer, error))
      return false;
    part.outer = std::move(outer);
  }
  part.holes.clear();
  for (const Json & item : *holes)
  {
    EditablePath hole;
    if (!readPath(item, hole, error))
      return false;
    part.holes.push_back(std::move(hole));
  }
  part.allowedRotations.clear();
  for (const Json & item : *rotations)
  {
    if (!item.is_number_integer())
    {
      error = "Некорректный черновик: поворот должен быть целым числом";
      return false;
    }
    try
    {
      part.allowedRotations.push_back(item.get<int>());
    }
    catch (const Json::exception &)
    {
      error = "Некорректный черновик: поворот находится вне допустимого диапазона";
      return false;
    }
  }
  return true;
}

/// Читает всю редактируемую модель, сохраняя временно некорректные предметные значения.
bool readDocument(const Json & value, EditablePolygonDocument & document, std::string & error)
{
  const Json * sheet = nullptr;
  const Json * manufacturing = nullptr;
  const Json * parts = nullptr;
  const Json * objective = nullptr;
  if (!keys(value, {"problemId", "sheet", "manufacturing", "parts", "objective"}, "неверные поля документа", error) ||
      !accept(aipackaging::solver::internal::readString(value, "problemId", document.problemId), "неверный идентификатор задачи",
              error) ||
      !objectField(value, "sheet", sheet, error) || !objectField(value, "manufacturing", manufacturing, error) ||
      !arrayField(value, "parts", parts, error) || !objectField(value, "objective", objective, error))
    return false;

  if (!keys(*sheet, {"entityId", "width", "height", "unit"}, "неверные поля листа", error) ||
      !entityId(*sheet, "entityId", document.sheet.id, error) ||
      !accept(aipackaging::solver::internal::readDouble(*sheet, "width", document.sheet.width), "неверная ширина листа", error) ||
      !accept(aipackaging::solver::internal::readDouble(*sheet, "height", document.sheet.height), "неверная высота листа",
              error) ||
      !accept(aipackaging::solver::internal::readString(*sheet, "unit", document.sheet.unit), "неверная единица листа", error))
    return false;

  if (!keys(*manufacturing, {"sheetMargin", "partSpacing", "kerf", "curveTolerance"}, "неверные поля производственных параметров",
            error) ||
      !accept(aipackaging::solver::internal::readDouble(*manufacturing, "sheetMargin", document.manufacturing.sheetMargin),
              "неверный отступ листа", error) ||
      !accept(aipackaging::solver::internal::readDouble(*manufacturing, "partSpacing", document.manufacturing.partSpacing),
              "неверный зазор деталей", error) ||
      !accept(aipackaging::solver::internal::readDouble(*manufacturing, "kerf", document.manufacturing.kerf),
              "неверная ширина реза", error) ||
      !accept(aipackaging::solver::internal::readDouble(*manufacturing, "curveTolerance", document.manufacturing.curveTolerance),
              "неверный допуск кривых", error))
    return false;

  if (!keys(*objective, {"type", "version"}, "неверные поля целевой функции", error) ||
      !accept(aipackaging::solver::internal::readString(*objective, "type", document.objective.type),
              "неверный вид целевой функции", error) ||
      !accept(aipackaging::solver::internal::readInt(*objective, "version", document.objective.version),
              "неверная версия целевой функции", error))
    return false;

  document.parts.clear();
  for (const Json & item : *parts)
  {
    EditablePart part;
    if (!readPart(item, part, error))
      return false;
    document.parts.push_back(std::move(part));
  }
  return true;
}

/// Читает метаданные источника и необязательный отпечаток исходного файла.
bool readMetadata(const Json & value, PolygonDraftMetadata & metadata, std::string & error)
{
  std::string source;
  if (!keys(value, {"sourceKind", "sourceIdentifier", "savedAtUtc", "generation", "baseFingerprint"}, "неверные поля метаданных",
            error) ||
      !accept(aipackaging::solver::internal::readString(value, "sourceKind", source), "неверный вид источника", error) ||
      !parseDraftSourceKind(source, metadata.source) ||
      !accept(aipackaging::solver::internal::readString(value, "sourceIdentifier", metadata.sourceIdentifier),
              "неверный идентификатор источника", error) ||
      !accept(aipackaging::solver::internal::readString(value, "savedAtUtc", metadata.savedAtUtc), "неверное время сохранения",
              error) ||
      !accept(aipackaging::solver::internal::readUint64(value, "generation", metadata.generation),
              "неверное поколение автосохранения", error))
  {
    if (error.empty())
      error = "Некорректный черновик: неизвестный вид источника `" + source + "`";
    return false;
  }
  static const std::regex utcPattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$)");
  if (!std::regex_match(metadata.savedAtUtc, utcPattern))
  {
    error = "Некорректный черновик: время должно быть задано в UTC";
    return false;
  }
  if (!value.contains("baseFingerprint"))
  {
    error = "Некорректный черновик: отсутствует поле `baseFingerprint`";
    return false;
  }
  if (value.at("baseFingerprint").is_null())
  {
    metadata.baseFingerprint.reset();
    return true;
  }
  const Json & fingerprint = value.at("baseFingerprint");
  DraftFileFingerprint parsed;
  if (!keys(fingerprint, {"size", "modifiedUnixNs"}, "неверные поля отпечатка файла", error) ||
      !accept(aipackaging::solver::internal::readUint64(fingerprint, "size", parsed.size), "неверный размер файла", error) ||
      !accept(aipackaging::solver::internal::readInt64(fingerprint, "modifiedUnixNs", parsed.modifiedUnixNs),
              "неверное время изменения файла", error))
    return false;
  metadata.baseFingerprint = parsed;
  return true;
}

/// Записывает точку в каноническое объектное представление.
Json writePoint(const EditablePoint & point)
{
  return {{"entityId", point.id.value}, {"x", point.x}, {"y", point.y}};
}

/// Записывает сегмент без неиспользуемых полей других видов.
Json writeSegment(const EditableSegment & segment)
{
  Json value = {{"entityId", segment.id.value}};
  if (segment.kind == EditableSegmentKind::Line)
    value["type"] = "line";
  else if (segment.kind == EditableSegmentKind::Arc)
  {
    value["type"] = "arc";
    value["center"] = writePoint(segment.center);
    value["clockwise"] = segment.clockwise;
  }
  else
  {
    value["type"] = "cubic_bezier";
    value["control1"] = writePoint(segment.control1);
    value["control2"] = writePoint(segment.control2);
  }
  return value;
}

/// Записывает упорядоченные вершины и сегменты одной цепочки.
Json writePath(const EditablePath & path)
{
  Json vertices = Json::array();
  Json segments = Json::array();
  for (const EditablePoint & point : path.vertices)
    vertices.push_back(writePoint(point));
  for (const EditableSegment & segment : path.segments)
    segments.push_back(writeSegment(segment));
  return {{"entityId", path.id.value}, {"closed", path.closed}, {"vertices", vertices}, {"segments", segments}};
}

/// Записывает один тип детали с необязательным внешним контуром.
Json writePart(const EditablePart & part)
{
  Json holes = Json::array();
  for (const EditablePath & hole : part.holes)
    holes.push_back(writePath(hole));
  return {{"entityId", part.id.value},
          {"partId", part.partId},
          {"quantity", part.quantity},
          {"outer", part.outer ? writePath(*part.outer) : Json(nullptr)},
          {"holes", holes},
          {"allowedRotations", part.allowedRotations}};
}

/// Записывает полную редактируемую модель без состояния интерфейса и решателя.
Json writeDocument(const EditablePolygonDocument & document)
{
  Json parts = Json::array();
  for (const EditablePart & part : document.parts)
    parts.push_back(writePart(part));
  return {{"problemId", document.problemId},
          {"sheet",
           {{"entityId", document.sheet.id.value},
            {"width", document.sheet.width},
            {"height", document.sheet.height},
            {"unit", document.sheet.unit}}},
          {"manufacturing",
           {{"sheetMargin", document.manufacturing.sheetMargin},
            {"partSpacing", document.manufacturing.partSpacing},
            {"kerf", document.manufacturing.kerf},
            {"curveTolerance", document.manufacturing.curveTolerance}}},
          {"parts", parts},
          {"objective", {{"type", document.objective.type}, {"version", document.objective.version}}}};
}

/// Проверяет только инварианты идентичности, обязательные для строгого формата черновика.
bool validIdentity(const EditablePolygonDocument & document, std::string & error)
{
  for (const DocumentDiagnostic & diagnostic : validateEditableDocument(document))
  {
    if (diagnostic.code == DocumentDiagnosticCode::InvalidEntityId ||
        diagnostic.code == DocumentDiagnosticCode::DuplicateEntityId ||
        diagnostic.code == DocumentDiagnosticCode::InvalidNextEntityId)
    {
      error = "Некорректный черновик: " + diagnostic.message;
      return false;
    }
  }
  return true;
}
} // namespace

/// Возвращает точный литерал происхождения для сохранения в формате черновика.
std::string_view toString(DraftSourceKind source) noexcept
{
  switch (source)
  {
    case DraftSourceKind::ProblemFile:
      return "problem_file";
    case DraftSourceKind::DraftFile:
      return "draft_file";
    case DraftSourceKind::Imported:
      return "imported";
    case DraftSourceKind::Recovered:
      return "recovered";
    case DraftSourceKind::Untitled:
      return "untitled";
  }
  return "untitled";
}

/// Сопоставляет известные литералы формата одному из видов происхождения.
bool parseDraftSourceKind(std::string_view text, DraftSourceKind & source) noexcept
{
  constexpr std::array values{DraftSourceKind::ProblemFile, DraftSourceKind::DraftFile, DraftSourceKind::Imported,
                              DraftSourceKind::Recovered, DraftSourceKind::Untitled};
  const auto found =
    std::find_if(values.begin(), values.end(), [text](DraftSourceKind value) { return toString(value) == text; });
  if (found == values.end())
    return false;
  source = *found;
  return true;
}

/// Строго разбирает корень, метаданные и модель, затем проверяет глобальную идентичность сущностей.
bool loadPolygonDraftFromText(const std::string & text, PolygonDraft & draft, std::string & error)
{
  try
  {
    Json root;
    int version = 0;
    if (!accept(aipackaging::solver::internal::parseDocument(text, root), "ошибка синтаксиса JSON", error) ||
        !keys(root, {"format", "version", "metadata", "nextEntityId", "document"}, "неверные поля корня", error) ||
        !accept(aipackaging::solver::internal::validateRootContract(root, draftFormat, {draftVersion}, version),
                "неподдерживаемый формат или версия", error))
      return false;
    const Json * metadata = nullptr;
    const Json * documentValue = nullptr;
    std::uint64_t nextEntityId = 0;
    PolygonDraft parsed;
    if (!objectField(root, "metadata", metadata, error) || !objectField(root, "document", documentValue, error) ||
        !accept(aipackaging::solver::internal::readUint64(root, "nextEntityId", nextEntityId), "неверный следующий идентификатор",
                error) ||
        !readMetadata(*metadata, parsed.metadata, error) || !readDocument(*documentValue, parsed.document, error) ||
        !parsed.document.restoreNextEntityId(nextEntityId) || !validIdentity(parsed.document, error))
    {
      if (error.empty())
        error = "Некорректный черновик: следующий идентификатор недопустим";
      return false;
    }
    draft = std::move(parsed);
    error.clear();
    return true;
  }
  catch (const std::exception & exception)
  {
    error = "Не удалось прочитать черновик: " + std::string(exception.what());
    return false;
  }
}

/// Читает файл общей строгой операцией и передаёт его содержимое разбору черновика.
bool loadPolygonDraftFromFile(const std::string & filePath, PolygonDraft & draft, std::string & error)
{
  std::string text;
  if (!accept(aipackaging::solver::internal::readTextFile(filePath, text), "не удалось прочитать файл", error))
    return false;
  return loadPolygonDraftFromText(text, draft, error);
}

/// Проверяет идентичность и формирует канонический JSON без отбрасывания локально некорректной геометрии.
bool savePolygonDraftToText(const PolygonDraft & draft, std::string & text, std::string & error)
{
  try
  {
    if (!validIdentity(draft.document, error))
      return false;
    static const std::regex utcPattern(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$)");
    if (!std::regex_match(draft.metadata.savedAtUtc, utcPattern))
    {
      error = "Некорректный черновик: время должно быть задано в UTC";
      return false;
    }
    Json fingerprint = nullptr;
    if (draft.metadata.baseFingerprint)
      fingerprint = {{"size", draft.metadata.baseFingerprint->size},
                     {"modifiedUnixNs", draft.metadata.baseFingerprint->modifiedUnixNs}};
    const Json root = {{"format", draftFormat},
                       {"version", draftVersion},
                       {"metadata",
                        {{"sourceKind", toString(draft.metadata.source)},
                         {"sourceIdentifier", draft.metadata.sourceIdentifier},
                         {"savedAtUtc", draft.metadata.savedAtUtc},
                         {"generation", draft.metadata.generation},
                         {"baseFingerprint", fingerprint}}},
                       {"nextEntityId", draft.document.nextEntityId()},
                       {"document", writeDocument(draft.document)}};
    text = root.dump(2) + '\n';
    error.clear();
    return true;
  }
  catch (const std::exception & exception)
  {
    error = "Не удалось сериализовать черновик: " + std::string(exception.what());
    return false;
  }
}

/// Сериализует документ в памяти и использует общую атомарную замену файла назначения.
bool savePolygonDraftToFile(const std::string & filePath, const PolygonDraft & draft, std::string & error)
{
  std::string text;
  if (!savePolygonDraftToText(draft, text, error))
    return false;
  return aipackaging::solver::internal::writeFileAtomically(filePath, text, error, {}, "черновика");
}
} // namespace aipackaging::editor
