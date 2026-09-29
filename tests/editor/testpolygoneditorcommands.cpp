#include <aipackaging/editor/polygon_editor_commands.h>
#include <gtest/gtest.h>

using namespace aipackaging::editor;

namespace
{
/// Выполняет одну подписанную команду и требует её принятия.
EditorCommandResult execute(PolygonEditorSession & session, EditorCommand command, std::string label = "Изменение")
{
  return session.execute({std::move(label), {std::move(command)}, std::nullopt});
}

/// Создаёт сессию с прямоугольной деталью исключительно средствами команд.
PolygonEditorSession rectangleSession()
{
  EditablePolygonDocument document;
  document.problemId = "editor";
  document.sheet.width = 100.0;
  document.sheet.height = 80.0;
  PolygonEditorSession session(std::move(document), true);
  EXPECT_TRUE(execute(session, AddPartCommand{"part", 1, {0, 90}}).accepted);
  const EntityId part = session.document().parts.front().id;
  EXPECT_TRUE(execute(session, CreatePathCommand{part, false, {0.0, 0.0}}).accepted);
  const EntityId path = session.document().parts.front().outer->id;
  EXPECT_TRUE(execute(session, AppendSegmentCommand{path, {20.0, 0.0}, {}}).accepted);
  EXPECT_TRUE(execute(session, AppendSegmentCommand{path, {20.0, 10.0}, {}}).accepted);
  EXPECT_TRUE(execute(session, AppendSegmentCommand{path, {0.0, 10.0}, {}}).accepted);
  EXPECT_TRUE(execute(session, ClosePathCommand{path, {}}).accepted);
  return session;
}
} // namespace

/// Проверяет создание корректного контура и обратимость всей последовательности.
TEST(PolygonEditorCommands, BuildsAndUndoesRectangle)
{
  PolygonEditorSession session = rectangleSession();
  EXPECT_FALSE(hasDocumentErrors(validateEditableDocument(session.document())));
  const std::uint64_t highWater = session.document().nextEntityId();
  ASSERT_TRUE(session.undo().accepted);
  EXPECT_FALSE(session.document().parts.front().outer->closed);
  EXPECT_GE(session.document().nextEntityId(), highWater);
  ASSERT_TRUE(session.redo().accepted);
  EXPECT_TRUE(session.document().parts.front().outer->closed);
}

/// Проверяет дугу, Bézier, перемещение служебной точки и смену вида сегмента.
TEST(PolygonEditorCommands, EditsCurvedSegmentsAndPoints)
{
  EditablePolygonDocument document;
  PolygonEditorSession session(std::move(document), true);
  ASSERT_TRUE(execute(session, AddPartCommand{"curve", 1, {0}}).accepted);
  const EntityId part = session.document().parts.front().id;
  ASSERT_TRUE(execute(session, CreatePathCommand{part, false, {0.0, 0.0}}).accepted);
  const EntityId path = session.document().parts.front().outer->id;
  EditorSegmentValue arc;
  arc.kind = EditableSegmentKind::Arc;
  arc.center = {5.0, 5.0};
  ASSERT_TRUE(execute(session, AppendSegmentCommand{path, {10.0, 0.0}, arc}).accepted);
  const EntityId segment = session.document().parts.front().outer->segments.front().id;
  const EntityId center = session.document().parts.front().outer->segments.front().center.id;
  EXPECT_TRUE(execute(session, MovePointCommand{center, {5.0, 6.0}}).accepted);
  EditorSegmentValue bezier;
  bezier.kind = EditableSegmentKind::CubicBezier;
  bezier.control1 = {2.0, 3.0};
  bezier.control2 = {8.0, 3.0};
  ASSERT_TRUE(execute(session, UpdateSegmentCommand{segment, bezier}).accepted);
  EXPECT_EQ(session.document().parts.front().outer->segments.front().kind, EditableSegmentKind::CubicBezier);
}

/// Проверяет, что удаление сегмента размыкает кольцо без потери остальных вершин и порядка сегментов.
TEST(PolygonEditorCommands, OpensClosedPathWhenSegmentIsDeleted)
{
  PolygonEditorSession session = rectangleSession();
  const EditablePath & original = *session.document().parts.front().outer;
  ASSERT_EQ(original.vertices.size(), 4U);
  ASSERT_EQ(original.segments.size(), 4U);
  const EntityId deleted = original.segments[1].id;
  const EntityId expectedFirstVertex = original.vertices[2].id;
  const EntityId expectedFirstSegment = original.segments[2].id;

  ASSERT_TRUE(execute(session, DeleteSegmentCommand{deleted}, "Удаление сегмента").accepted);
  const EditablePath & path = *session.document().parts.front().outer;
  EXPECT_FALSE(path.closed);
  ASSERT_EQ(path.vertices.size(), 4U);
  ASSERT_EQ(path.segments.size(), 3U);
  EXPECT_EQ(path.vertices.front().id, expectedFirstVertex);
  EXPECT_EQ(path.segments.front().id, expectedFirstSegment);

  ASSERT_TRUE(session.undo().accepted);
  EXPECT_TRUE(session.document().parts.front().outer->closed);
  EXPECT_EQ(session.document().parts.front().outer->segments[1].id, deleted);
}

/// Проверяет групповой перенос и атомарный отказ при повторном идентификаторе точки.
TEST(PolygonEditorCommands, TranslatesPointSelectionAtomically)
{
  PolygonEditorSession session = rectangleSession();
  const auto first = session.document().parts.front().outer->vertices[0];
  const auto second = session.document().parts.front().outer->vertices[1];
  ASSERT_TRUE(execute(session, TranslatePointsCommand{{first.id, second.id}, 3.0, -2.0}, "Перенос").accepted);
  EXPECT_DOUBLE_EQ(session.document().parts.front().outer->vertices[0].x, first.x + 3.0);
  EXPECT_DOUBLE_EQ(session.document().parts.front().outer->vertices[1].y, second.y - 2.0);

  const EditablePolygonDocument before = session.document();
  EXPECT_FALSE(execute(session, TranslatePointsCommand{{first.id, first.id}, 1.0, 1.0}).accepted);
  EXPECT_DOUBLE_EQ(session.document().parts.front().outer->vertices[0].x, before.parts.front().outer->vertices[0].x);
  EXPECT_EQ(session.history().undoLabel, "Перенос");
}

/// Проверяет атомарный отказ пакета при отсутствующей цели.
TEST(PolygonEditorCommands, RejectsBatchAtomically)
{
  PolygonEditorSession session;
  const std::uint64_t before = session.document().nextEntityId();
  EditorCommandBatch batch{"Пакет", {AddPartCommand{"temporary", 1, {0}}, DeletePartCommand{{999}}}, std::nullopt};
  const EditorCommandResult result = session.execute(batch);
  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(session.document().parts.empty());
  EXPECT_EQ(session.document().nextEntityId(), before);
  EXPECT_FALSE(session.history().canUndo);
}

/// Проверяет новые идентификаторы копии и их непереиспользование после отмены.
TEST(PolygonEditorCommands, DuplicatesWithFreshMonotonicIdentifiers)
{
  PolygonEditorSession session = rectangleSession();
  const EntityId source = session.document().parts.front().id;
  ASSERT_TRUE(execute(session, DuplicatePartCommand{source, "copy"}, "Дублирование").accepted);
  const EntityId copy = session.document().parts.back().id;
  EXPECT_GT(copy.value, source.value);
  const std::uint64_t highWater = session.document().nextEntityId();
  ASSERT_TRUE(session.undo().accepted);
  ASSERT_TRUE(execute(session, AddPartCommand{"new", 1, {0}}).accepted);
  EXPECT_GE(session.document().parts.back().id.value, highWater);
  EXPECT_FALSE(session.history().canRedo);
}

/// Проверяет чистую точку и объединение последовательных обновлений одного жеста.
TEST(PolygonEditorCommands, TracksCleanPointAndCoalescesGesture)
{
  PolygonEditorSession session;
  EXPECT_FALSE(session.history().dirty);
  const std::uint64_t gesture = session.beginGesture();
  ASSERT_NE(gesture, 0U);
  ASSERT_TRUE(session.execute({"Лист", {SetSheetCommand{10.0, 10.0}}, gesture}).accepted);
  ASSERT_TRUE(session.execute({"Лист", {SetSheetCommand{20.0, 20.0}}, gesture}).accepted);
  session.finishGesture(gesture);
  EXPECT_TRUE(session.history().dirty);
  ASSERT_TRUE(session.undo().accepted);
  EXPECT_DOUBLE_EQ(session.document().sheet.width, 0.0);
  ASSERT_TRUE(session.redo().accepted);
  session.markSaved();
  EXPECT_FALSE(session.history().dirty);
}

/// Проверяет отмену незавершённого непрерывного изменения.
TEST(PolygonEditorCommands, CancelsContinuousGesture)
{
  PolygonEditorSession session;
  const std::uint64_t gesture = session.beginGesture();
  ASSERT_TRUE(session.execute({"Перемещение", {SetSheetCommand{10.0, 20.0}}, gesture}).accepted);
  ASSERT_TRUE(session.cancelGesture(gesture).accepted);
  EXPECT_DOUBLE_EQ(session.document().sheet.width, 0.0);
  EXPECT_FALSE(session.history().canUndo);
}

/// Проверяет ограничение истории и сохранение отмены для последних двухсот действий.
TEST(PolygonEditorCommands, LimitsHistoryToTwoHundredTransactions)
{
  PolygonEditorSession session;
  for (int index = 0; index < 205; ++index)
    ASSERT_TRUE(execute(session, SetProblemIdCommand{std::to_string(index)}).accepted);
  int undone = 0;
  while (session.undo().accepted)
    ++undone;
  EXPECT_EQ(undone, 200);
}
