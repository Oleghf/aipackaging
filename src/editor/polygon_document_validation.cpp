#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_set>

#include <aipackaging/editor/polygon_document_validation.h>

namespace aipackaging::editor
{
namespace
{
/// Добавляет диагностику с единообразно заполненными полями.
void add(std::vector<DocumentDiagnostic> & result, DocumentDiagnosticCode code, EntityId entity, std::string message,
         DiagnosticSeverity severity = DiagnosticSeverity::Error)
{
  result.push_back({code, severity, entity, std::move(message)});
}

/// Регистрирует идентификатор и сообщает о нулевом либо повторном значении.
void registerId(EntityId id, std::unordered_set<std::uint64_t> & identifiers, std::uint64_t & maximum,
                std::vector<DocumentDiagnostic> & result)
{
  if (!id)
  {
    add(result, DocumentDiagnosticCode::InvalidEntityId, id, "Сущности документа требуется ненулевой идентификатор");
    return;
  }
  maximum = std::max(maximum, id.value);
  if (!identifiers.insert(id.value).second)
    add(result, DocumentDiagnosticCode::DuplicateEntityId, id, "Идентификатор сущности документа повторяется");
}

/// Проверяет конечность координат одной устойчиво идентифицированной точки.
void validatePoint(const EditablePoint & point, std::unordered_set<std::uint64_t> & identifiers, std::uint64_t & maximum,
                   std::vector<DocumentDiagnostic> & result)
{
  registerId(point.id, identifiers, maximum, result);
  if (!std::isfinite(point.x) || !std::isfinite(point.y))
    add(result, DocumentDiagnosticCode::InvalidCoordinate, point.id, "Координаты точки должны быть конечными числами");
}

/// Проверяет идентификаторы, координаты и связность одной редактируемой цепочки.
void validatePath(const EditablePath & path, std::unordered_set<std::uint64_t> & identifiers, std::uint64_t & maximum,
                  std::vector<DocumentDiagnostic> & result)
{
  registerId(path.id, identifiers, maximum, result);
  if (path.vertices.empty())
    add(result, DocumentDiagnosticCode::EmptyPath, path.id, "Контур не содержит вершин");
  for (const EditablePoint & vertex : path.vertices)
    validatePoint(vertex, identifiers, maximum, result);
  for (const EditableSegment & segment : path.segments)
  {
    registerId(segment.id, identifiers, maximum, result);
    if (segment.kind == EditableSegmentKind::Arc)
      validatePoint(segment.center, identifiers, maximum, result);
    else if (segment.kind == EditableSegmentKind::CubicBezier)
    {
      validatePoint(segment.control1, identifiers, maximum, result);
      validatePoint(segment.control2, identifiers, maximum, result);
    }
  }
  const std::size_t expected = path.closed ? path.vertices.size() : (path.vertices.empty() ? 0 : path.vertices.size() - 1);
  if (path.segments.size() != expected)
    add(result, DocumentDiagnosticCode::InvalidPathTopology, path.id,
        "Число сегментов не соответствует числу вершин и признаку замкнутости");
  if (!path.closed)
    add(result, DocumentDiagnosticCode::OpenPath, path.id, "Контур должен быть замкнут");
  if (path.closed && path.vertices.size() < 3)
    add(result, DocumentDiagnosticCode::EmptyPath, path.id, "Замкнутый контур должен содержать не менее трёх вершин");
}
} // namespace

/// Последовательно проверяет идентичность, структуру и локальные числовые ограничения документа.
std::vector<DocumentDiagnostic> validateEditableDocument(const EditablePolygonDocument & document)
{
  std::vector<DocumentDiagnostic> result;
  std::unordered_set<std::uint64_t> identifiers;
  std::uint64_t maximum = 0;
  registerId(document.sheet.id, identifiers, maximum, result);
  if (document.problemId.empty())
    add(result, DocumentDiagnosticCode::EmptyProblemId, {}, "Задаче требуется непустой идентификатор");
  if (!std::isfinite(document.sheet.width) || !std::isfinite(document.sheet.height) || document.sheet.width <= 0.0 ||
      document.sheet.height <= 0.0 || document.sheet.unit != "mm")
    add(result, DocumentDiagnosticCode::InvalidSheet, document.sheet.id,
        "Лист должен иметь положительные конечные размеры в миллиметрах");
  const auto & manufacturing = document.manufacturing;
  if (!std::isfinite(manufacturing.sheetMargin) || !std::isfinite(manufacturing.partSpacing) ||
      !std::isfinite(manufacturing.kerf) || !std::isfinite(manufacturing.curveTolerance) || manufacturing.sheetMargin < 0.0 ||
      manufacturing.partSpacing < 0.0 || manufacturing.kerf < 0.0 || manufacturing.curveTolerance < 0.001 ||
      manufacturing.curveTolerance > 0.05)
    add(result, DocumentDiagnosticCode::InvalidManufacturing, document.sheet.id,
        "Производственные параметры находятся вне допустимого диапазона");
  if (manufacturing.kerf > 0.0)
    add(result, DocumentDiagnosticCode::KerfIgnored, document.sheet.id,
        "Ширина реза сохраняется, но пока не влияет на размещение", DiagnosticSeverity::Warning);
  if (document.objective.type != "valuable_right_remnant" || document.objective.version != 1)
    add(result, DocumentDiagnosticCode::UnsupportedObjective, {}, "Целевая функция документа не поддерживается");
  if (document.parts.empty())
    add(result, DocumentDiagnosticCode::EmptyParts, {}, "Документ должен содержать хотя бы один тип детали");

  std::set<std::string> partNames;
  for (const EditablePart & part : document.parts)
  {
    registerId(part.id, identifiers, maximum, result);
    if (part.partId.empty())
      add(result, DocumentDiagnosticCode::EmptyPartId, part.id, "Типу детали требуется непустой идентификатор");
    else if (!partNames.insert(part.partId).second)
      add(result, DocumentDiagnosticCode::DuplicatePartId, part.id, "Идентификатор типа детали повторяется");
    if (part.quantity == 0)
      add(result, DocumentDiagnosticCode::InvalidQuantity, part.id, "Количество экземпляров должно быть положительным");
    const std::set<int> rotations(part.allowedRotations.begin(), part.allowedRotations.end());
    if (part.allowedRotations.empty() || rotations.size() != part.allowedRotations.size() ||
        std::any_of(rotations.begin(), rotations.end(),
                    [](int value) { return value != 0 && value != 90 && value != 180 && value != 270; }))
      add(result, DocumentDiagnosticCode::InvalidRotations, part.id,
          "Повороты должны быть непустым уникальным подмножеством 0, 90, 180 и 270 градусов");
    if (!part.outer)
      add(result, DocumentDiagnosticCode::MissingOuterPath, part.id, "У детали отсутствует внешний контур");
    else
      validatePath(*part.outer, identifiers, maximum, result);
    for (const EditablePath & hole : part.holes)
      validatePath(hole, identifiers, maximum, result);
  }
  if (document.nextEntityId() <= maximum)
    add(result, DocumentDiagnosticCode::InvalidNextEntityId, {},
        "Следующий идентификатор должен быть больше всех сохранённых идентификаторов");
  return result;
}

/// Ищет хотя бы одну диагностику с блокирующим уровнем ошибки.
bool hasDocumentErrors(const std::vector<DocumentDiagnostic> & diagnostics) noexcept
{
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [](const DocumentDiagnostic & item) { return item.severity == DiagnosticSeverity::Error; });
}
} // namespace aipackaging::editor
