#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include <aipackaging/editor/polygon_document.h>
#include <aipackaging/editor/polygon_document_validation.h>
#include <gtest/gtest.h>

using namespace aipackaging::editor;

namespace
{
/// Создаёт замкнутый прямоугольный контур с устойчивыми идентификаторами.
EditablePath rectangle(EditablePolygonDocument & document, double width, double height)
{
  EditablePath result;
  result.id = document.allocateEntityId();
  result.closed = true;
  for (const auto [x, y] : {std::pair{0.0, 0.0}, {width, 0.0}, {width, height}, {0.0, height}})
    result.vertices.push_back({document.allocateEntityId(), x, y});
  for (int index = 0; index < 4; ++index)
    result.segments.push_back({document.allocateEntityId(), EditableSegmentKind::Line});
  return result;
}

/// Создаёт локально корректный документ с одной прямоугольной деталью.
EditablePolygonDocument validDocument()
{
  EditablePolygonDocument document;
  document.problemId = "editable-problem";
  document.sheet.width = 100.0;
  document.sheet.height = 80.0;
  EditablePart part;
  part.id = document.allocateEntityId();
  part.partId = "part";
  part.quantity = 2;
  part.allowedRotations = {0, 90};
  part.outer = rectangle(document, 20.0, 10.0);
  document.parts.push_back(std::move(part));
  return document;
}
} // namespace

/// Проверяет монотонную выдачу идентификаторов без повторного использования.
TEST(EditablePolygonDocument, AllocatesStableMonotonicIdentifiers)
{
  EditablePolygonDocument document;
  const EntityId first = document.allocateEntityId();
  const EntityId second = document.allocateEntityId();
  EXPECT_LT(first.value, second.value);
  EXPECT_EQ(document.nextEntityId(), second.value + 1);
  EXPECT_FALSE(document.restoreNextEntityId(second.value));
  EXPECT_TRUE(document.restoreNextEntityId(100));
  EXPECT_EQ(document.allocateEntityId().value, 100U);
}

/// Проверяет отсутствие блокирующих ошибок у обычного замкнутого документа.
TEST(EditablePolygonDocument, AcceptsLocallyValidDocument)
{
  const auto diagnostics = validateEditableDocument(validDocument());
  EXPECT_FALSE(hasDocumentErrors(diagnostics));
}

/// Проверяет сохранение открытой цепочки как диагностируемого черновика.
TEST(EditablePolygonDocument, ReportsOpenPathWithoutRejectingTheValueObject)
{
  EditablePolygonDocument document = validDocument();
  document.parts.front().outer->closed = false;
  document.parts.front().outer->segments.pop_back();
  const auto diagnostics = validateEditableDocument(document);
  ASSERT_TRUE(hasDocumentErrors(diagnostics));
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::OpenPath; }),
            diagnostics.end());
}

/// Проверяет обнаружение повторного идентификатора и повторного имени детали.
TEST(EditablePolygonDocument, RejectsDuplicateIdentifiersAndPartNames)
{
  EditablePolygonDocument document = validDocument();
  EditablePart duplicate = document.parts.front();
  duplicate.partId = document.parts.front().partId;
  document.parts.push_back(std::move(duplicate));
  const auto diagnostics = validateEditableDocument(document);
  EXPECT_TRUE(hasDocumentErrors(diagnostics));
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::DuplicateEntityId; }),
            diagnostics.end());
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::DuplicatePartId; }),
            diagnostics.end());
}

/// Проверяет дугу, Bézier и предупреждение о пока не применяемой ширине реза.
TEST(EditablePolygonDocument, ValidatesCurveControlsAndSeparatesWarnings)
{
  EditablePolygonDocument document = validDocument();
  EditablePath & path = *document.parts.front().outer;
  path.segments[0].kind = EditableSegmentKind::Arc;
  path.segments[0].center = {document.allocateEntityId(), 10.0, 0.0};
  path.segments[1].kind = EditableSegmentKind::CubicBezier;
  path.segments[1].control1 = {document.allocateEntityId(), 20.0, 3.0};
  path.segments[1].control2 = {document.allocateEntityId(), 20.0, 7.0};
  document.manufacturing.kerf = 0.2;
  const auto diagnostics = validateEditableDocument(document);
  EXPECT_FALSE(hasDocumentErrors(diagnostics));
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::KerfIgnored; }),
            diagnostics.end());
}

/// Проверяет нечисловые координаты, пустые повороты и некорректный источник идентификаторов.
TEST(EditablePolygonDocument, RejectsInvalidNumbersRotationsAndNextIdentifier)
{
  EditablePolygonDocument document = validDocument();
  document.parts.front().outer->vertices.front().x = std::numeric_limits<double>::infinity();
  document.parts.front().allowedRotations.clear();
  ASSERT_TRUE(document.restoreNextEntityId(500));
  document.parts.front().outer->vertices.front().id = {500};
  const auto diagnostics = validateEditableDocument(document);
  EXPECT_TRUE(hasDocumentErrors(diagnostics));
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::InvalidCoordinate; }),
            diagnostics.end());
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::InvalidRotations; }),
            diagnostics.end());
  EXPECT_NE(std::find_if(diagnostics.begin(), diagnostics.end(),
                         [](const auto & item) { return item.code == DocumentDiagnosticCode::InvalidNextEntityId; }),
            diagnostics.end());
}
