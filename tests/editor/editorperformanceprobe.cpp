#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <aipackaging/editor/polygon_document_validation.h>
#include <aipackaging/editor/polygon_editor_commands.h>
#include <polygon_artifact_store.h>
#include <polygon_editable_document_gateway.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#else
#include <fstream>

#include <unistd.h>
#endif

namespace
{
using namespace aipackaging::editor;
using Clock = std::chrono::steady_clock;

/// Добавляет точку с новым устойчивым идентификатором.
EditablePoint point(EditablePolygonDocument & document, double x, double y)
{
  return {document.allocateEntityId(), x, y};
}

/// Создаёт замкнутый многоугольник из линейных сегментов.
EditablePath regularPath(EditablePolygonDocument & document, std::size_t count, double radius)
{
  EditablePath path;
  path.id = document.allocateEntityId();
  path.closed = true;
  for (std::size_t index = 0; index < count; ++index)
  {
    const double angle = 2.0 * std::acos(-1.0) * static_cast<double>(index) / static_cast<double>(count);
    path.vertices.push_back(point(document, radius + radius * std::cos(angle), radius + radius * std::sin(angle)));
    path.segments.push_back({document.allocateEntityId(), EditableSegmentKind::Line});
  }
  return path;
}

/// Создаёт прямоугольный путь с одним прямолинейным сегментом Bézier.
EditablePath bezierRectangle(EditablePolygonDocument & document, double width, double height)
{
  EditablePath path;
  path.id = document.allocateEntityId();
  path.closed = true;
  path.vertices = {point(document, 0.0, 0.0), point(document, width, 0.0), point(document, width, height),
                   point(document, 0.0, height)};
  EditableSegment bezier;
  bezier.id = document.allocateEntityId();
  bezier.kind = EditableSegmentKind::CubicBezier;
  bezier.control1 = point(document, width / 3.0, 0.0);
  bezier.control2 = point(document, width * 2.0 / 3.0, 0.0);
  path.segments = {bezier,
                   {document.allocateEntityId(), EditableSegmentKind::Line},
                   {document.allocateEntityId(), EditableSegmentKind::Line},
                   {document.allocateEntityId(), EditableSegmentKind::Line}};
  return path;
}

/// Создаёт окружность из четырёх дуг с общим центром.
EditablePath arcCircle(EditablePolygonDocument & document, double radius)
{
  EditablePath path;
  path.id = document.allocateEntityId();
  path.closed = true;
  path.vertices = {point(document, radius * 2.0, radius), point(document, radius, radius * 2.0), point(document, 0.0, radius),
                   point(document, radius, 0.0)};
  for (int index = 0; index < 4; ++index)
  {
    EditableSegment arc;
    arc.id = document.allocateEntityId();
    arc.kind = EditableSegmentKind::Arc;
    arc.center = point(document, radius, radius);
    path.segments.push_back(arc);
  }
  return path;
}

/// Создаёт точно корректный документ с заданным числом сегментов и типов деталей.
EditablePolygonDocument makeDocument(std::size_t segmentCount, std::size_t partCount, std::string identifier)
{
  EditablePolygonDocument document;
  document.problemId = std::move(identifier);
  document.sheet.width = 2000.0;
  document.sheet.height = 1000.0;
  document.manufacturing = {0.0, 0.0, 0.0, 0.05};
  std::size_t usedSegments = 0;
  for (std::size_t index = 0; index < partCount; ++index)
  {
    EditablePart part;
    part.id = document.allocateEntityId();
    part.partId = "part-" + std::to_string(index + 1);
    part.quantity = static_cast<std::uint32_t>(100 / partCount + (index < 100 % partCount ? 1 : 0));
    part.allowedRotations = {0, 90};
    if (index == 0)
      part.outer = arcCircle(document, 20.0);
    else if (index == 1)
      part.outer = bezierRectangle(document, 50.0, 30.0);
    else if (index == 2)
    {
      part.outer = regularPath(document, 4, 40.0);
      part.holes.push_back(regularPath(document, 4, 10.0));
      for (EditablePoint & value : part.holes.back().vertices)
      {
        value.x += 30.0;
        value.y += 30.0;
      }
    }
    else
    {
      const std::size_t remainingParts = partCount - index;
      const std::size_t count = (segmentCount - usedSegments + remainingParts - 1) / remainingParts;
      part.outer = regularPath(document, count, 30.0 + static_cast<double>(index % 5));
    }
    usedSegments += part.outer ? part.outer->segments.size() : 0;
    for (const EditablePath & hole : part.holes)
      usedSegments += hole.segments.size();
    document.parts.push_back(std::move(part));
  }
  return document;
}

/// Возвращает медиану упорядоченной выборки миллисекунд.
double median(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

/// Возвращает ближайший верхний элемент 95-го процентиля.
double percentile95(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  return values[std::min(values.size() - 1, (values.size() * 95 + 99) / 100 - 1)];
}

/// Измеряет текущий резидентный объём процесса в байтах, когда платформа его предоставляет.
std::uint64_t residentBytes()
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS counters{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))
         ? static_cast<std::uint64_t>(counters.WorkingSetSize)
         : 0;
#else
  std::ifstream input("/proc/self/statm");
  std::uint64_t total = 0;
  std::uint64_t resident = 0;
  return input >> total >> resident ? resident * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE)) : 0;
#endif
}

/// Измеряет локальную и точную проверку после двух прогревов.
std::vector<double> validationSamples(const EditablePolygonDocument & document,
                                      const std::shared_ptr<PolygonArtifactStore> & store)
{
  LocalPolygonEditableDocumentGateway gateway(store);
  for (int warmup = 0; warmup < 2; ++warmup)
    gateway.compileImported(document, {}, std::nullopt);
  std::vector<double> samples;
  for (int sample = 0; sample < 7; ++sample)
  {
    const auto started = Clock::now();
    const auto local = validateEditableDocument(document);
    const PolygonEditableDocumentLoadResult exact = gateway.compileImported(document, {}, std::nullopt);
    if (hasDocumentErrors(local) || !exact.compiled)
    {
      const std::vector<DocumentDiagnostic> & diagnostics = hasDocumentErrors(local) ? local : exact.diagnostics;
      throw std::runtime_error(diagnostics.empty() ? "Синтетический документ не прошёл точную проверку"
                                                   : diagnostics.front().message);
    }
    samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
  }
  return samples;
}

/// Измеряет построение полного набора диагностик для заведомо вырожденного крупного документа.
std::vector<double> invalidValidationSamples(EditablePolygonDocument document)
{
  EditablePath & outer = *document.parts.front().outer;
  outer.vertices[1].x = outer.vertices.front().x;
  outer.vertices[1].y = outer.vertices.front().y;
  for (int warmup = 0; warmup < 2; ++warmup)
    static_cast<void>(validateEditableDocument(document));
  std::vector<double> samples;
  for (int sample = 0; sample < 7; ++sample)
  {
    const auto started = Clock::now();
    const std::vector<DocumentDiagnostic> diagnostics = validateEditableDocument(document);
    if (!hasDocumentErrors(diagnostics))
      throw std::runtime_error("Некорректный синтетический документ не дал диагностик");
    samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
  }
  return samples;
}

/// Измеряет подтверждение, отмену и повтор небольшого изменения крупного документа.
std::vector<double> historySamples(const EditablePolygonDocument & document)
{
  PolygonEditorSession session(document);
  const EntityId vertex = session.document().parts.front().outer->vertices.front().id;
  std::vector<double> samples;
  for (int index = 0; index < 200; ++index)
  {
    const auto started = Clock::now();
    EditorCommandBatch batch{"Измерительное перемещение", {}, std::nullopt};
    batch.commands.push_back(MovePointCommand{vertex, {0.001 * static_cast<double>(index + 1), 20.0}});
    if (!session.execute(batch).accepted || !session.undo().accepted || !session.redo().accepted)
      throw std::runtime_error("История редактора отклонила измерительную команду");
    samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
  }
  return samples;
}
} // namespace

/// Генерирует крупные документы, печатает измерения и возвращает ошибку при превышении ворот.
int main(int argc, char ** argv)
{
  try
  {
    const bool ci = argc > 1 && std::string(argv[1]) == "--ci";
    const double multiplier = ci ? 4.0 : 1.0;
    const EditablePolygonDocument medium = makeDocument(500, 20, "editor-500");
    const EditablePolygonDocument large = makeDocument(1000, 40, "editor-1000");
    const auto store = std::make_shared<PolygonArtifactStore>();
    const std::vector<double> validation = validationSamples(large, store);
    const std::vector<double> invalidValidation = invalidValidationSamples(large);
    const std::vector<double> history = historySamples(large);
    const double validationMedian = median(validation);
    const double validationP95 = percentile95(validation);
    const double invalidValidationP95 = percentile95(invalidValidation);
    const double historyMedian = median(history);
    const double historyP95 = percentile95(history);
    const std::uint64_t memory = residentBytes();
    std::cout << "{\"документы\":[\"" << medium.problemId << "\",\"" << large.problemId
              << "\"],\"validationMedianMs\":" << validationMedian << ",\"validationP95Ms\":" << validationP95
              << ",\"invalidValidationP95Ms\":" << invalidValidationP95 << ",\"historyMedianMs\":" << historyMedian
              << ",\"historyP95Ms\":" << historyP95 << ",\"residentBytes\":" << memory << "}\n";
    if (validationP95 > 3000.0 * multiplier || invalidValidationP95 > 3000.0 * multiplier || historyP95 > 250.0 * multiplier ||
        (memory != 0 && memory > static_cast<std::uint64_t>(512.0 * 1024.0 * 1024.0 * multiplier)))
      return 1;
    return 0;
  }
  catch (const std::exception & exception)
  {
    std::cerr << "Ошибка измерения редактора: " << exception.what() << '\n';
    return 1;
  }
}
