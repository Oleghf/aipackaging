#include "polygon_editable_document_gateway.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

#include <aipackaging/editor/polygon_draft_io.h>
#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>

#include "polygon_artifact_store_internal.h"
#include "polygon_presentation_mapper.h"

using namespace aipackaging::editor;
using namespace aipackaging::solver;

namespace
{
/// Читает файл целиком без преобразования переводов строк.
bool readFile(const std::string & filePath, std::string & text, std::string & error)
{
  std::ifstream input(filePath, std::ios::binary);
  if (!input)
  {
    error = "Не удалось открыть документ";
    return false;
  }
  text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if (!input && !input.eof())
  {
    error = "Не удалось прочитать документ";
    return false;
  }
  return true;
}

/// Возвращает отпечаток обычного файла либо пустое значение при недоступном источнике.
std::optional<PolygonSourceFingerprint> fingerprint(const std::string & filePath)
{
  std::error_code error;
  const std::filesystem::path path(filePath);
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status))
    return std::nullopt;
  const std::uint64_t size = std::filesystem::file_size(path, error);
  if (error)
    return std::nullopt;
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error)
    return std::nullopt;
  return PolygonSourceFingerprint{size,
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(modified.time_since_epoch()).count()};
}

/// Формирует текущую отметку UTC в представлении RFC 3339 с секундной точностью.
std::string utcNow()
{
  const std::time_t now = std::time(nullptr);
  std::tm value{};
#ifdef _WIN32
  gmtime_s(&value, &now);
#else
  gmtime_r(&now, &value);
#endif
  std::ostringstream output;
  output << std::put_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

/// Сопоставляет нейтральное происхождение прикладного слоя литералу черновика.
DraftSourceKind toDraftSource(PolygonDocumentSource source)
{
  switch (source)
  {
    case PolygonDocumentSource::ProblemFile:
      return DraftSourceKind::ProblemFile;
    case PolygonDocumentSource::Draft:
      return DraftSourceKind::DraftFile;
    case PolygonDocumentSource::Imported:
      return DraftSourceKind::Imported;
    case PolygonDocumentSource::RecoveredDraft:
      return DraftSourceKind::Recovered;
    case PolygonDocumentSource::None:
    case PolygonDocumentSource::Untitled:
      return DraftSourceKind::Untitled;
  }
  return DraftSourceKind::Untitled;
}

/// Сопоставляет сохранённое происхождение черновика нейтральному прикладному виду.
PolygonDocumentSource fromDraftSource(DraftSourceKind source)
{
  switch (source)
  {
    case DraftSourceKind::ProblemFile:
      return PolygonDocumentSource::ProblemFile;
    case DraftSourceKind::DraftFile:
      return PolygonDocumentSource::Draft;
    case DraftSourceKind::Imported:
      return PolygonDocumentSource::Imported;
    case DraftSourceKind::Recovered:
      return PolygonDocumentSource::RecoveredDraft;
    case DraftSourceKind::Untitled:
      return PolygonDocumentSource::Untitled;
  }
  return PolygonDocumentSource::Untitled;
}

/// Переносит точку из контракта решателя в редактируемую модель с новым идентификатором.
EditablePoint toEditablePoint(EditablePolygonDocument & document, const PolygonPointMm & source)
{
  return {document.allocateEntityId(), source.x, source.y};
}

/// Детерминированно назначает идентификаторы вершинам, сегментам и управляющим точкам пути.
EditablePath toEditablePath(EditablePolygonDocument & document, const PolygonPath & source)
{
  EditablePath path;
  path.id = document.allocateEntityId();
  path.closed = true;
  path.vertices.push_back(toEditablePoint(document, source.start));
  for (std::size_t index = 0; index < source.segments.size(); ++index)
  {
    const PolygonSegment & sourceSegment = source.segments[index];
    if (index + 1 != source.segments.size())
      path.vertices.push_back(toEditablePoint(document, sourceSegment.end));
    EditableSegment segment;
    segment.id = document.allocateEntityId();
    segment.clockwise = sourceSegment.clockwise;
    if (sourceSegment.kind == PolygonSegmentKind::Arc)
    {
      segment.kind = EditableSegmentKind::Arc;
      segment.center = toEditablePoint(document, sourceSegment.center);
    }
    else if (sourceSegment.kind == PolygonSegmentKind::CubicBezier)
    {
      segment.kind = EditableSegmentKind::CubicBezier;
      segment.control1 = toEditablePoint(document, sourceSegment.control1);
      segment.control2 = toEditablePoint(document, sourceSegment.control2);
    }
    path.segments.push_back(std::move(segment));
  }
  return path;
}

/// Детерминированно преобразует строгую задачу в полностью идентифицированный документ.
EditablePolygonDocument toEditableDocument(const PolygonProblem & problem)
{
  EditablePolygonDocument document;
  document.problemId = problem.problemId;
  document.sheet.width = problem.sheet.width;
  document.sheet.height = problem.sheet.height;
  document.sheet.unit = problem.sheet.unit;
  document.manufacturing = {problem.manufacturing.sheetMargin, problem.manufacturing.partSpacing, problem.manufacturing.kerf,
                            problem.manufacturing.curveTolerance};
  document.objective = {problem.objective.type, problem.objective.version};
  for (const PolygonPart & source : problem.parts)
  {
    EditablePart part;
    part.id = document.allocateEntityId();
    part.partId = source.id;
    part.quantity = source.quantity;
    part.allowedRotations = source.allowedRotations;
    part.outer = toEditablePath(document, source.outer);
    for (const PolygonPath & hole : source.holes)
      part.holes.push_back(toEditablePath(document, hole));
    document.parts.push_back(std::move(part));
  }
  return document;
}

/// Переносит редактируемую точку в контракт решателя без идентификатора.
PolygonPointMm toProblemPoint(const EditablePoint & source)
{
  return {source.x, source.y};
}

/// Преобразует замкнутую цепочку в путь с конечной точкой каждого сегмента.
PolygonPath toProblemPath(const EditablePath & source)
{
  PolygonPath path;
  if (source.vertices.empty())
    return path;
  path.start = toProblemPoint(source.vertices.front());
  path.segments.reserve(source.segments.size());
  for (std::size_t index = 0; index < source.segments.size(); ++index)
  {
    const EditableSegment & sourceSegment = source.segments[index];
    PolygonSegment segment;
    segment.end = toProblemPoint(source.vertices[(index + 1) % source.vertices.size()]);
    segment.clockwise = sourceSegment.clockwise;
    if (sourceSegment.kind == EditableSegmentKind::Arc)
    {
      segment.kind = PolygonSegmentKind::Arc;
      segment.center = toProblemPoint(sourceSegment.center);
    }
    else if (sourceSegment.kind == EditableSegmentKind::CubicBezier)
    {
      segment.kind = PolygonSegmentKind::CubicBezier;
      segment.control1 = toProblemPoint(sourceSegment.control1);
      segment.control2 = toProblemPoint(sourceSegment.control2);
    }
    path.segments.push_back(segment);
  }
  return path;
}

/// Преобразует локально корректный документ в неизменяемый контракт задачи.
PolygonProblem toProblem(const EditablePolygonDocument & document)
{
  PolygonProblem problem;
  problem.problemId = document.problemId;
  problem.sheet = {document.sheet.width, document.sheet.height, document.sheet.unit};
  problem.manufacturing = {document.manufacturing.sheetMargin, document.manufacturing.partSpacing, document.manufacturing.kerf,
                           document.manufacturing.curveTolerance};
  problem.objective = {document.objective.type, document.objective.version};
  for (const EditablePart & source : document.parts)
  {
    PolygonPart part;
    part.id = source.partId;
    part.quantity = source.quantity;
    part.allowedRotations = source.allowedRotations;
    part.outer = toProblemPath(*source.outer);
    for (const EditablePath & hole : source.holes)
      part.holes.push_back(toProblemPath(hole));
    problem.parts.push_back(std::move(part));
  }
  return problem;
}

/// Формирует сведения документа даже тогда, когда точная геометрия ещё не построена.
PolygonDocumentSummary editableSummary(const EditablePolygonDocument & document)
{
  PolygonDocumentSummary summary;
  summary.sheetWidth = document.sheet.width;
  summary.sheetHeight = document.sheet.height;
  summary.sheetMargin = document.manufacturing.sheetMargin;
  summary.partSpacing = document.manufacturing.partSpacing;
  summary.kerf = document.manufacturing.kerf;
  for (std::size_t index = 0; index < document.parts.size(); ++index)
  {
    const EditablePart & source = document.parts[index];
    PolygonPartSummary part;
    part.id = source.partId;
    part.quantity = source.quantity;
    part.allowedRotations = source.allowedRotations;
    part.colorIndex = index;
    summary.parts.push_back(std::move(part));
  }
  return summary;
}

/// Выполняет локальную проверку, точную нормализацию и регистрацию неизменяемого снимка.
void compileDocument(const std::shared_ptr<PolygonArtifactStore> & store, PolygonEditableDocumentLoadResult & result)
{
  result.diagnostics = validateEditableDocument(result.document);
  result.problemId = result.document.problemId;
  result.summary = editableSummary(result.document);
  result.summary.sourceIdentifier = result.sourceIdentifier;
  result.scene.sheetWidth = result.document.sheet.width;
  result.scene.sheetHeight = result.document.sheet.height;
  result.scene.sheetMargin = result.document.manufacturing.sheetMargin;
  if (hasDocumentErrors(result.diagnostics))
    return;
  PolygonProblem problem = toProblem(result.document);
  std::string error;
  std::unique_ptr<PolygonEnvironment> created = PolygonEnvironment::Create(problem, error);
  if (!created)
  {
    result.diagnostics.push_back({DocumentDiagnosticCode::ExactGeometryRejected, DiagnosticSeverity::Error,
                                  result.document.sheet.id, "Точная проверка геометрии отклонила документ: " + error});
    return;
  }
  std::shared_ptr<PolygonEnvironment> environment(std::move(created));
  PolygonDocumentLoadResult compiled;
  compiled.success = true;
  compiled.problemId = problem.problemId;
  compiled.document = store->addDocument(problem, environment);
  const auto record = store->document(compiled.document);
  compiled.summary = aipackaging::desktop::detail::buildDocumentSummary(*record);
  compiled.summary.sourceIdentifier = result.sourceIdentifier;
  aipackaging::desktop::detail::buildPresentation(*record, nullptr, compiled.scene, compiled.unplacedInstances);
  result.summary = compiled.summary;
  result.scene = compiled.scene;
  result.unplacedInstances = compiled.unplacedInstances;
  result.compiled = std::move(compiled);
}

/// Создаёт прикладной отпечаток из метаданных формата черновика.
std::optional<PolygonSourceFingerprint> fromDraftFingerprint(const std::optional<DraftFileFingerprint> & source)
{
  if (!source)
    return std::nullopt;
  return PolygonSourceFingerprint{source->size, source->modifiedUnixNs};
}

/// Создаёт метаданные формата черновика из нейтрального прикладного отпечатка.
std::optional<DraftFileFingerprint> toDraftFingerprint(const std::optional<PolygonSourceFingerprint> & source)
{
  if (!source)
    return std::nullopt;
  return DraftFileFingerprint{source->size, source->modifiedUnixNs};
}
} // namespace

/// Сохраняет разделяемое процессное хранилище для компиляции документов.
LocalPolygonEditableDocumentGateway::LocalPolygonEditableDocumentGateway(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Определяет корневой формат, читает предметную модель и компилирует её при полной корректности.
PolygonEditableDocumentLoadResult LocalPolygonEditableDocumentGateway::load(const std::string & filePath)
{
  PolygonEditableDocumentLoadResult result;
  std::string text;
  if (!readFile(filePath, text, result.error))
    return result;
  const std::string format = detectJsonFormat(text);
  if (format == "aipackaging.polygon_problem")
  {
    const PolygonProblemLoadResult loaded = loadPolygonProblemFromText(text);
    if (!loaded.success)
    {
      result.error = loaded.error;
      return result;
    }
    result.document = toEditableDocument(loaded.problem);
    result.source = PolygonDocumentSource::ProblemFile;
    result.sourceIdentifier = filePath;
    result.baseFingerprint = fingerprint(filePath);
  }
  else if (format == "aipackaging.polygon_draft")
  {
    PolygonDraft draft;
    if (!loadPolygonDraftFromText(text, draft, result.error))
      return result;
    result.document = std::move(draft.document);
    result.source = PolygonDocumentSource::Draft;
    result.sourceIdentifier = filePath;
    result.generation = draft.metadata.generation;
    result.baseFingerprint = fingerprint(filePath);
  }
  else
  {
    result.error = "Поддерживаются только `polygon_problem` v1 и `polygon_draft` v1";
    return result;
  }
  result.success = true;
  compileDocument(store_, result);
  return result;
}

/// Загружает черновик и сравнивает сохранённый отпечаток с текущим исходным файлом.
PolygonEditableDocumentLoadResult LocalPolygonEditableDocumentGateway::loadRecovery(const std::string & filePath)
{
  PolygonEditableDocumentLoadResult result;
  PolygonDraft draft;
  if (!loadPolygonDraftFromFile(filePath, draft, result.error))
    return result;
  result.success = true;
  result.document = std::move(draft.document);
  result.source = PolygonDocumentSource::RecoveredDraft;
  result.sourceIdentifier = draft.metadata.sourceIdentifier;
  result.generation = draft.metadata.generation;
  result.baseFingerprint = fromDraftFingerprint(draft.metadata.baseFingerprint);
  if (result.baseFingerprint && !result.sourceIdentifier.empty())
    result.sourceChanged = fingerprint(result.sourceIdentifier) != result.baseFingerprint;
  compileDocument(store_, result);
  return result;
}

/// Отвергает локальные ошибки, повторяет точную нормализацию и только затем атомарно записывает задачу.
PolygonDocumentOperationResult LocalPolygonEditableDocumentGateway::saveProblem(const std::string & filePath,
                                                                                const EditablePolygonDocument & document)
{
  if (hasDocumentErrors(validateEditableDocument(document)))
    return {false, "Документ содержит блокирующие ошибки"};
  PolygonProblem problem = toProblem(document);
  std::string error;
  if (!PolygonEnvironment::Create(problem, error))
    return {false, error};
  return {savePolygonProblemToFile(filePath, problem, error), error};
}

/// Добавляет метаданные происхождения и передаёт канонический документ атомарному адаптеру черновика.
PolygonDocumentOperationResult
LocalPolygonEditableDocumentGateway::saveDraft(const std::string & filePath, const EditablePolygonDocument & document,
                                               PolygonDocumentSource source, const std::string & sourceIdentifier,
                                               std::uint64_t generation, std::optional<PolygonSourceFingerprint> baseFingerprint)
{
  PolygonDraft draft;
  draft.document = document;
  draft.metadata.source = toDraftSource(source);
  draft.metadata.sourceIdentifier = sourceIdentifier;
  draft.metadata.savedAtUtc = utcNow();
  draft.metadata.generation = generation;
  draft.metadata.baseFingerprint = toDraftFingerprint(baseFingerprint);
  std::string error;
  return {savePolygonDraftToFile(filePath, draft, error), error};
}

/// Возвращает только проверенный отпечаток обычного файла.
std::optional<PolygonSourceFingerprint> LocalPolygonEditableDocumentGateway::sourceFingerprint(const std::string & filePath)
{
  return fingerprint(filePath);
}

/// Разбирает метаданные без публикации документа и диагностирует изменение исходного файла.
PolygonRecoveryCandidate LocalPolygonEditableDocumentGateway::inspectRecovery(const std::string & filePath)
{
  PolygonRecoveryCandidate result;
  result.autosavePath = filePath;
  std::error_code statusError;
  const auto status = std::filesystem::symlink_status(filePath, statusError);
  if ((statusError && statusError == std::errc::no_such_file_or_directory) || (!statusError && !std::filesystem::exists(status)))
    return result;
  result.present = true;
  if (statusError || !std::filesystem::is_regular_file(status))
  {
    result.error = "Автоматический черновик не является обычным файлом";
    return result;
  }
  PolygonDraft draft;
  if (!loadPolygonDraftFromFile(filePath, draft, result.error))
    return result;
  result.restorable = true;
  result.sourceIdentifier = draft.metadata.sourceIdentifier;
  result.savedAtUtc = draft.metadata.savedAtUtc;
  const auto saved = fromDraftFingerprint(draft.metadata.baseFingerprint);
  if (saved && !result.sourceIdentifier.empty())
    result.sourceChanged = fingerprint(result.sourceIdentifier) != saved;
  return result;
}

/// Проверяет вид назначения и удаляет существующий обычный файл без следов частичной операции.
PolygonDocumentOperationResult LocalPolygonEditableDocumentGateway::removeRecovery(const std::string & filePath)
{
  if (filePath.empty())
    return {true, {}};
  std::error_code error;
  const auto status = std::filesystem::symlink_status(filePath, error);
  if ((error && error == std::errc::no_such_file_or_directory) || (!error && !std::filesystem::exists(status)))
    return {true, {}};
  if (error)
    return {false, "Не удалось проверить автоматический черновик"};
  if (!std::filesystem::is_regular_file(status))
    return {false, "Автоматический черновик должен быть обычным файлом"};
  if (!std::filesystem::remove(filePath, error) || error)
    return {false, "Не удалось удалить автоматический черновик"};
  return {true, {}};
}
