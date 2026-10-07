#include <algorithm>
#include <cmath>
#include <map>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFocusEvent>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QWheelEvent>
#include <QWizard>

#include <aipackaging/editor/polygon_draft_io.h>
#include <gtest/gtest.h>
#include <polygoncanvaswidget.h>
#include <polygondxfimportwizard.h>
#include <polygonworkspacewidget.h>

#include "support.h"

namespace acceptance
{
namespace
{
using aipackaging::editor::EntityId;

/// Возвращает проверенный внешний контур первой детали либо явную ошибку подготовки сценария.
const aipackaging::editor::EditablePath & outerPath(const Desktop & app)
{
  const auto document = app.workspace->snapshot().editableDocument;
  if (!document || document->parts.empty() || !document->parts.front().outer)
    throw std::runtime_error("В тестовом документе отсутствует внешний контур");
  return *document->parts.front().outer;
}

/// Выбирает сущность в дереве настоящим щелчком после раскрытия её родителей.
void selectEntity(Desktop & app, EntityId id)
{
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  if (tabs->currentIndex() != 0)
    click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  auto * tree = widget<QTreeWidget>(app.window, "editorEntityTree");
  for (QTreeWidgetItemIterator iterator(tree); *iterator; ++iterator)
    if ((*iterator)->data(0, Qt::UserRole + 2).toULongLong() == id.value)
    {
      auto * item = *iterator;
      for (auto * parent = item->parent(); parent; parent = parent->parent())
        parent->setExpanded(true);
      tree->scrollToItem(item);
      QCoreApplication::processEvents();
      click(tree->viewport(), tree->visualItemRect(item).center());
      return;
    }
  throw std::runtime_error("Сущность не найдена в дереве редактора");
}

/// Ищет поле по подписи существующей формы без изменения продуктовых имён объектов.
QWidget * labelledField(Desktop & app, const QString & label)
{
  for (auto * form : app.window.findChildren<QFormLayout *>())
    for (int row = 0; row < form->rowCount(); ++row)
    {
      auto * item = form->itemAt(row, QFormLayout::LabelRole);
      auto * text = item ? qobject_cast<QLabel *>(item->widget()) : nullptr;
      if (text && text->text() == label)
        return form->itemAt(row, QFormLayout::FieldRole)->widget();
    }
  throw std::runtime_error("Не найдена подпись числовой формы");
}

/// Нажимает видимую команду с заданным русским текстом.
void textButton(Desktop & app, const QString & text)
{
  for (auto * button : app.window.findChildren<QAbstractButton *>())
    if (button->text() == text && button->isVisible())
    {
      click(button);
      return;
    }
  throw std::runtime_error("Не найдена кнопка числовой формы");
}

/// Заполняет конечную точку через клавиатуру, не отправляя редакторные команды напрямую.
void coordinates(Desktop & app, double x, double y)
{
  enter(widget<QDoubleSpinBox>(app.window, "editorPointX"), QString::number(x));
  enter(widget<QDoubleSpinBox>(app.window, "editorPointY"), QString::number(y));
}

/// Выбирает вид сегмента обычными клавишами списка.
void segmentKind(Desktop & app, int kind)
{
  auto * combo = widget<QComboBox>(app.window, "editorSegmentKind");
  key(combo, Qt::Key_Home);
  for (int index = 0; index < kind; ++index)
    key(combo, Qt::Key_Down);
}

/// Создаёт лист и прямоугольник целиком через диалог и числовые формы.
void createRectangle(Desktop & app)
{
  whenModal(
    [](QWidget * dialog)
    {
      enter(widget<QLineEdit>(*dialog, "newProblemId"), "numeric-acceptance");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetWidth"), "100");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetHeight"), "100");
      click(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok));
    });
  action(app.window, "createDocumentAction");
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  click(widget<QWidget>(app.window, "addPartButton"));
  selectEntity(app, app.workspace->snapshot().editableDocument->parts.front().id);
  coordinates(app, 10, 10);
  click(widget<QWidget>(app.window, "createOuterPathButton"));
  const auto path = outerPath(app).id;
  for (const QPointF point : {QPointF(90, 10), QPointF(90, 90), QPointF(10, 90)})
  {
    selectEntity(app, path);
    coordinates(app, point.x(), point.y());
    click(widget<QWidget>(app.window, "appendSegmentButton"));
  }
  selectEntity(app, path);
  textButton(app, QStringLiteral("Замкнуть"));
}

/// Посылает одно событие мыши, сохраняя удерживаемые кнопки и модификаторы.
void mouse(QWidget * target, QEvent::Type type, QPoint position, Qt::MouseButton button, Qt::MouseButtons held,
           Qt::KeyboardModifiers modifiers = {})
{
  QMouseEvent event(type, position, target->mapToGlobal(position), button, held, modifiers);
  QApplication::sendEvent(target, &event);
  QCoreApplication::processEvents();
}

/// Выполняет перенос или рамку с настоящими нажатием, движением и отпусканием.
// Порядок начальной и конечной точки совпадает с последовательностью событий мыши во всех вызовах.
void drag(QWidget * target, QPoint from, QPoint to, // NOLINT(bugprone-easily-swappable-parameters)
          Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = {})
{
  mouse(target, QEvent::MouseButtonPress, from, button, button, modifiers);
  mouse(target, QEvent::MouseMove, to, Qt::NoButton, button, modifiers);
  mouse(target, QEvent::MouseButtonRelease, to, button, {}, modifiers);
}

/// Возвращает экранную точку после штатного вписывания квадратного листа 100 мм.
QPoint fitted(PolygonCanvasWidget * canvas, double x, double y)
{
  const double scale = std::min(canvas->width() - 48.0, canvas->height() - 48.0) / 100.0;
  return {qRound(canvas->width() / 2.0 + (x - 50) * scale), qRound(canvas->height() / 2.0 - (y - 50) * scale)};
}

/// Доставляет вращение колеса относительно фиксированного указателя.
void wheel(PolygonCanvasWidget * canvas, QPoint position, int delta)
{
  QWheelEvent event(position, canvas->mapToGlobal(position), {}, QPoint(0, delta), {}, {}, Qt::NoScrollPhase, false);
  QApplication::sendEvent(canvas, &event);
  QCoreApplication::processEvents();
}

/// Принимает разобранный DXF через все страницы и обе стадии последней кнопки.
void acceptImport(Desktop & app)
{
  if (!waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Ready; }))
    throw std::runtime_error("Не завершился анализ DXF");
  auto * wizard = widget<PolygonDxfImportWizard>(app.window, "polygonDxfImportWizard");
  for (int page = 0; page < 4; ++page)
    key(wizard->button(QWizard::NextButton), Qt::Key_Space);
  key(wizard->button(QWizard::FinishButton), Qt::Key_Space);
  if (!waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Completed; }))
    throw std::runtime_error("Не завершилось построение DXF");
  key(wizard->button(QWizard::FinishButton), Qt::Key_Space);
}

/// Собирает все устойчивые геометрические точки для независимой проверки одинакового переноса.
std::map<std::uint64_t, QPointF> allPoints(const aipackaging::editor::EditablePolygonDocument & document)
{
  std::map<std::uint64_t, QPointF> result;
  const auto addPath = [&](const aipackaging::editor::EditablePath & path)
  {
    for (const auto & point : path.vertices)
      result[point.id.value] = {point.x, point.y};
    for (const auto & segment : path.segments)
      for (const auto & point : {segment.center, segment.control1, segment.control2})
        if (point.id)
          result[point.id.value] = {point.x, point.y};
  };
  for (const auto & part : document.parts)
  {
    if (part.outer)
      addPath(*part.outer);
    for (const auto & hole : part.holes)
      addPath(hole);
  }
  return result;
}
} // namespace

/// Изолирует дополнительные сквозные проверки от файлов и настроек остальных сценариев.
class GuiExtendedAcceptance : public testing::Test
{
protected:
  QTemporaryDir directory_;
  /// Очищает только профиль текущего тестового процесса.
  void SetUp() override
  {
    ASSERT_TRUE(directory_.isValid());
    QSettings().clear();
  }
};

/// Строит линии, дуги и Bézier числовыми формами, меняет свойства и проверяет сохранённый результат.
TEST_F(GuiExtendedAcceptance, NumericalFormsCreateCurvesHolesAndProperties)
{
  Desktop app(directory_.path());
  createRectangle(app);
  ASSERT_TRUE(app.workspace->snapshot().documentValid);
  const auto partId = app.workspace->snapshot().editableDocument->parts.front().id;
  selectEntity(app, partId);
  enter(widget<QLineEdit>(app.window, "editorPartId"), QStringLiteral("числовая деталь"));
  enter(widget<QSpinBox>(app.window, "editorQuantity"), "3");
  enter(widget<QLineEdit>(app.window, "editorRotations"), "0,90");
  click(widget<QWidget>(app.window, "applyPartButton"));
  EXPECT_EQ(app.workspace->snapshot().editableDocument->parts.front().quantity, 3U);
  EXPECT_EQ(app.workspace->snapshot().editableDocument->parts.front().allowedRotations, (std::vector<int>{0, 90}));
  for (const int kind : {1, 2})
  {
    selectEntity(app, partId);
    coordinates(app, kind == 1 ? 30 : 25, kind == 1 ? 55 : 20);
    click(widget<QWidget>(app.window, "createHolePathButton"));
    const auto hole = app.workspace->snapshot().editableDocument->parts.front().holes.back().id;
    selectEntity(app, hole);
    segmentKind(app, kind);
    coordinates(app, kind == 1 ? 70 : 45, kind == 1 ? 55 : 20);
    enter(labelledField(app, QStringLiteral("X центра / C1")), kind == 1 ? "50" : "25");
    enter(labelledField(app, QStringLiteral("Y центра / C1")), kind == 1 ? "55" : "25");
    if (kind == 2)
    {
      enter(labelledField(app, QStringLiteral("X C2")), "45");
      enter(labelledField(app, QStringLiteral("Y C2")), "25");
    }
    click(widget<QWidget>(app.window, "appendSegmentButton"));
    selectEntity(app, hole);
    segmentKind(app, kind == 1 ? 1 : 0);
    if (kind == 1)
    {
      enter(labelledField(app, QStringLiteral("X центра / C1")), "50");
      enter(labelledField(app, QStringLiteral("Y центра / C1")), "55");
    }
    textButton(app, QStringLiteral("Замкнуть"));
    ASSERT_TRUE(app.workspace->snapshot().documentValid) << app.workspace->snapshot().statusText;
    const auto & path = app.workspace->snapshot().editableDocument->parts.front().holes.back();
    EXPECT_EQ(path.vertices.size(), 2U);
    EXPECT_EQ(path.segments.size(), 2U);
    EXPECT_TRUE(path.closed);
  }
  enter(widget<QDoubleSpinBox>(app.window, "editorSheetMargin"), "2");
  enter(widget<QDoubleSpinBox>(app.window, "editorPartSpacing"), "1");
  auto * kerf = widget<QDoubleSpinBox>(app.window, "editorKerf");
  enter(kerf, kerf->locale().toString(0.2));
  click(widget<QWidget>(app.window, "applyDocumentPropertiesButton"));
  ASSERT_TRUE(app.workspace->snapshot().documentValid);
  const QString path = directory_.filePath("numeric.json");
  chooseFile(path);
  key(&app.window, Qt::Key_S, Qt::ControlModifier);
  app.open(path);
  ASSERT_TRUE(app.workspace->snapshot().documentValid);
  EXPECT_EQ(app.workspace->snapshot().editableDocument->parts.front().holes.size(), 2U);
  EXPECT_DOUBLE_EQ(app.workspace->snapshot().editableDocument->manufacturing.kerf, 0.2);
  const auto originalPoints = allPoints(*app.workspace->snapshot().editableDocument);
  selectEntity(app, app.workspace->snapshot().editableDocument->parts.front().id);
  action(app.window, "editorSelectTool");
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  key(canvas, Qt::Key_F);
  drag(canvas, fitted(canvas, 50, 10), fitted(canvas, 55, 15), Qt::LeftButton, Qt::AltModifier);
  const auto movedPoints = allPoints(*app.workspace->snapshot().editableDocument);
  ASSERT_EQ(movedPoints.size(), originalPoints.size());
  const QPointF displacement = movedPoints.begin()->second - originalPoints.begin()->second;
  EXPECT_NEAR(displacement.x(), 5, 0.5);
  for (const auto & [id, point] : originalPoints)
  {
    EXPECT_NEAR((movedPoints.at(id) - point).x(), displacement.x(), 1e-8);
    EXPECT_NEAR((movedPoints.at(id) - point).y(), displacement.y(), 1e-8);
  }
  key(canvas, Qt::Key_Z, Qt::ControlModifier);
  EXPECT_EQ(allPoints(*app.workspace->snapshot().editableDocument), originalPoints);
  key(canvas, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
  EXPECT_EQ(allPoints(*app.workspace->snapshot().editableDocument), movedPoints);
}

/// Проверяет рамки обоих направлений, перенос выбранного контура и откат прерванного жеста.
TEST_F(GuiExtendedAcceptance, FrameGroupDragUndoAndCaptureLoss)
{
  Desktop app(directory_.path());
  createRectangle(app);
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  action(app.window, "editorSelectTool");
  key(canvas, Qt::Key_F);
  const auto original = outerPath(app);
  std::vector<std::uint64_t> selection;
  QObject::connect(canvas, &PolygonCanvasWidget::editorEntitiesSelected, canvas, [&](const auto & value) { selection = value; });
  for (const bool crossing : {false, true})
  {
    SCOPED_TRACE(crossing);
    const auto left = fitted(canvas, 5, 95), right = fitted(canvas, 95, 5);
    drag(canvas, crossing ? right : left, crossing ? left : right);
    ASSERT_FALSE(selection.empty());
    const auto from = fitted(canvas, 50, 10), to = fitted(canvas, 55, 15);
    drag(canvas, from, to, Qt::LeftButton, Qt::AltModifier);
    const auto moved = outerPath(app);
    ASSERT_EQ(moved.vertices.size(), original.vertices.size());
    const double dx = moved.vertices.front().x - original.vertices.front().x;
    const double dy = moved.vertices.front().y - original.vertices.front().y;
    EXPECT_NEAR(dx, 5, 0.5);
    EXPECT_NEAR(dy, 5, 0.5);
    for (std::size_t index = 0; index < moved.vertices.size(); ++index)
    {
      EXPECT_NEAR(moved.vertices[index].x - original.vertices[index].x, dx, 1e-8);
      EXPECT_NEAR(moved.vertices[index].y - original.vertices[index].y, dy, 1e-8);
    }
    key(canvas, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_DOUBLE_EQ(outerPath(app).vertices.front().x, original.vertices.front().x);
  }
  for (const auto interruption : {QEvent::UngrabMouse, QEvent::FocusOut, QEvent::WindowDeactivate})
  {
    selectEntity(app, original.id);
    const auto from = fitted(canvas, 50, 10), to = fitted(canvas, 55, 15);
    mouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    mouse(canvas, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::AltModifier);
    if (interruption == QEvent::FocusOut)
    {
      QFocusEvent event(QEvent::FocusOut);
      QApplication::sendEvent(canvas, &event);
    }
    else
    {
      QEvent event(interruption);
      QApplication::sendEvent(canvas, &event);
    }
    mouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, {});
    EXPECT_DOUBLE_EQ(outerPath(app).vertices.front().x, original.vertices.front().x);
    EXPECT_DOUBLE_EQ(outerPath(app).vertices.front().y, original.vertices.front().y);
  }
}

/// Измеряет координату под мышью при увеличении, навигации и вписывании из дерева без перехвата текста.
TEST_F(GuiExtendedAcceptance, NavigationFocusAndModalPriority)
{
  Desktop app(directory_.path());
  createRectangle(app);
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  QPointF observed;
  QObject::connect(canvas, &PolygonCanvasWidget::cursorPositionChanged, canvas,
                   [&](double x, double y, bool) { observed = {x, y}; });
  const QPoint cursor = fitted(canvas, 40, 35);
  mouse(canvas, QEvent::MouseMove, cursor, Qt::NoButton, {});
  // Первая координата меняет подсказку строки состояния; ждём её обычной компоновки.
  mouse(canvas, QEvent::MouseMove, cursor, Qt::NoButton, {});
  const auto initial = observed;
  for (const int delta : {240, -480, 240})
  {
    wheel(canvas, cursor, delta);
    mouse(canvas, QEvent::MouseMove, cursor, Qt::NoButton, {});
    EXPECT_NEAR(observed.x(), initial.x(), 1e-8);
    EXPECT_NEAR(observed.y(), initial.y(), 1e-8);
  }
  for (const bool space : {false, true})
  {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, {}), release(QEvent::KeyRelease, Qt::Key_Space, {});
    if (space)
      QApplication::sendEvent(canvas, &press);
    drag(canvas, cursor, cursor + QPoint(20, 10), space ? Qt::LeftButton : Qt::MiddleButton);
    if (space)
      QApplication::sendEvent(canvas, &release);
    mouse(canvas, QEvent::MouseMove, cursor, Qt::NoButton, {});
    EXPECT_GT((observed - initial).manhattanLength(), 1);
    key(widget<QTreeWidget>(app.window, "editorEntityTree"), Qt::Key_F);
    mouse(canvas, QEvent::MouseMove, cursor, Qt::NoButton, {});
    EXPECT_NEAR(observed.x(), initial.x(), 1e-8);
    EXPECT_NEAR(observed.y(), initial.y(), 1e-8);
  }
  auto * field = widget<QLineEdit>(app.window, "editorProblemId");
  const auto oldText = field->text();
  key(field, Qt::Key_End);
  key(field, Qt::Key_F, {}, "f");
  EXPECT_EQ(field->text(), oldText + 'f');
  const auto revision = app.workspace->snapshot().editableDocument;
  chooseFile({}, true);
  key(field, Qt::Key_O, Qt::ControlModifier);
  EXPECT_EQ(app.workspace->snapshot().editableDocument, revision);
  EXPECT_NE(app.workspace->snapshot().state, PolygonWorkspaceState::Running);
}

/// Использует сохранённый только тестом профиль для повторного импорта после настоящего перезапуска.
int recentWorker(const QStringList & arguments)
{
  const auto offset = arguments.indexOf("--recent-worker");
  if (offset < 0 || arguments.size() < offset + 4)
    return 2;
  Desktop app(arguments[offset + 2]);
  const QString & source = arguments[offset + 3];
  if (arguments[offset + 1] == "write")
  {
    chooseFile(source);
    action(app.window, "importDxfAction");
    acceptImport(app);
    QSettings().sync();
    return app.workspace->snapshot().documentSource == PolygonDocumentSource::Imported ? 0 : 3;
  }
  auto * recents = widget<QListWidget>(app.window, "recentProblemList");
  for (int index = 0; index < recents->count(); ++index)
    if (recents->item(index)->data(Qt::UserRole + 1).toString() == source)
    {
      click(recents->viewport(), recents->visualItemRect(recents->item(index)).center());
      key(recents, Qt::Key_Return);
      if (!waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Ready; }))
        return 4;
      auto * wizard = widget<PolygonDxfImportWizard>(app.window, "polygonDxfImportWizard");
      if (wizard->currentId() != 0 || app.workspace->snapshot().hasDocument)
        return 5;
      key(wizard, Qt::Key_Escape);
      return app.workspace->snapshot().hasDocument ? 6 : 0;
    }
  return 7;
}

/// Передаёт один изолированный профиль двум процессам и проверяет новое открытие мастера с первой страницы.
TEST_F(GuiExtendedAcceptance, RecentDxfSurvivesProcessRestart)
{
  const QString source = directory_.filePath(QStringLiteral("недавний 漢字.DXF"));
  ASSERT_TRUE(QFile::copy(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/dxf/square-mm.dxf"), source));
  for (const QString & mode : {QStringLiteral("write"), QStringLiteral("read")})
  {
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--recent-worker", mode, directory_.path(), source});
    ASSERT_TRUE(child.waitForFinished(20000));
    EXPECT_EQ(child.exitCode(), 0) << mode.toStdString() << child.readAllStandardError().toStdString();
  }
}

/// Проверяет реальную геометрию пересекающей рамки на трёх видах сегментов и нескольких масштабах полотна.
TEST_F(GuiExtendedAcceptance, CrossingSelectionUsesCurvesAtSeveralZooms)
{
  using namespace aipackaging::editor;
  for (const auto kind : {EditableSegmentKind::Line, EditableSegmentKind::Arc, EditableSegmentKind::CubicBezier})
    for (const int zoom : {-240, 0, 240})
    {
      SCOPED_TRACE(static_cast<int>(kind));
      SCOPED_TRACE(zoom);
      QSettings().clear();
      PolygonDraft draft;
      draft.metadata.savedAtUtc = "2026-10-07T00:00:00Z";
      auto & document = draft.document;
      document.problemId = "selection-fixture";
      document.sheet.width = document.sheet.height = 100;
      EditablePart part;
      part.id = document.allocateEntityId();
      part.partId = "curve";
      part.quantity = 1;
      part.allowedRotations = {0};
      EditablePath path;
      path.id = document.allocateEntityId();
      path.vertices = {{document.allocateEntityId(), 20, 20},
                       {document.allocateEntityId(), 80, kind == EditableSegmentKind::Line ? 80.0 : 20.0}};
      EditableSegment segment;
      segment.id = document.allocateEntityId();
      segment.kind = kind;
      if (kind == EditableSegmentKind::Arc)
      {
        path.vertices.front().y = path.vertices.back().y = 50;
        segment.center = {document.allocateEntityId(), 50, 50};
        segment.clockwise = true;
      }
      if (kind == EditableSegmentKind::CubicBezier)
      {
        segment.control1 = {document.allocateEntityId(), 20, 80};
        segment.control2 = {document.allocateEntityId(), 80, 80};
      }
      path.segments = {segment};
      part.outer = path;
      document.parts = {part};
      const QString file = directory_.filePath("crossing.aipdraft.json");
      std::string error;
      ASSERT_TRUE(savePolygonDraftToFile(file.toStdString(), draft, error)) << error;
      Desktop app(directory_.path());
      app.open(file);
      auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
      click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
      selectEntity(app, part.id);
      action(app.window, "editorSelectTool");
      auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
      key(canvas, Qt::Key_F);
      wheel(canvas, canvas->rect().center(), zoom);
      const double factor = std::pow(1.0015, zoom);
      const auto screen = [&](double x, double y)
      {
        const QPoint base = fitted(canvas, x, y), center = canvas->rect().center();
        return center + QPoint(qRound((base.x() - center.x()) * factor), qRound((base.y() - center.y()) * factor));
      };
      std::vector<std::uint64_t> selected;
      QObject::connect(canvas, &PolygonCanvasWidget::editorEntitiesSelected, canvas,
                       [&](const auto & value) { selected = value; });
      const double emptyY = kind == EditableSegmentKind::Line ? 65 : 30;
      drag(canvas, screen(40, emptyY), screen(25, emptyY + 8));
      EXPECT_EQ(std::find(selected.begin(), selected.end(), segment.id.value), selected.end());
      const double hitY = kind == EditableSegmentKind::Line ? 50 : kind == EditableSegmentKind::Arc ? 80 : 65;
      drag(canvas, screen(56, hitY - 4), screen(44, hitY + 4));
      EXPECT_NE(std::find(selected.begin(), selected.end(), segment.id.value), selected.end());
      // Та же малая рамка слева направо не содержит сегмент целиком.
      drag(canvas, screen(44, hitY + 4), screen(56, hitY - 4));
      EXPECT_EQ(std::find(selected.begin(), selected.end(), segment.id.value), selected.end());
      if (kind == EditableSegmentKind::Arc)
      {
        drag(canvas, screen(56, 16), screen(44, 24));
        EXPECT_EQ(std::find(selected.begin(), selected.end(), segment.id.value), selected.end())
          << "Дуга по часовой стрелке от левой вершины проходит выше центра, а не ниже";
      }
    }
}

/// Проверяет переключение точек через `Ctrl`, привязку к сетке и её временное отключение через `Alt`.
TEST_F(GuiExtendedAcceptance, ControlSelectionAndGridSnapping)
{
  Desktop app(directory_.path());
  createRectangle(app);
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  action(app.window, "editorSelectTool");
  key(canvas, Qt::Key_F);
  std::vector<std::uint64_t> selected;
  QObject::connect(canvas, &PolygonCanvasWidget::editorEntitiesSelected, canvas, [&](const auto & value) { selected = value; });
  const auto outer = outerPath(app);
  click(canvas, fitted(canvas, 10, 10));
  for (int repeat = 0; repeat < 2; ++repeat)
  {
    const auto second = fitted(canvas, 90, 10);
    mouse(canvas, QEvent::MouseButtonPress, second, Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    mouse(canvas, QEvent::MouseButtonRelease, second, Qt::LeftButton, {}, Qt::ControlModifier);
    EXPECT_EQ(selected.size(), repeat == 0 ? 2U : 1U);
  }
  auto * grid = widget<QCheckBox>(app.window, "editorGridSnap");
  key(grid, Qt::Key_Space);
  ASSERT_TRUE(grid->isChecked());
  for (const bool alt : {false, true})
  {
    selectEntity(app, outer.vertices.front().id);
    drag(canvas, fitted(canvas, 10, 10), fitted(canvas, 21, 21), Qt::LeftButton, alt ? Qt::AltModifier : Qt::NoModifier);
    const auto moved = outerPath(app).vertices.front();
    EXPECT_NEAR(moved.x, alt ? 21 : 20, alt ? 0.5 : 1e-8);
    EXPECT_NEAR(moved.y, alt ? 21 : 20, alt ? 0.5 : 1e-8);
    key(canvas, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_DOUBLE_EQ(outerPath(app).vertices.front().x, 10);
  }
  EXPECT_TRUE(QSettings().value("editor/snapGrid").toBool());
}

/// Выполняет общие сочетания при фокусе на полотне, дереве, форме и настройках запуска.
TEST_F(GuiExtendedAcceptance, KeyboardFocusMatrixAndSingleDelivery)
{
  for (int focus = 0; focus < 4; ++focus)
  {
    SCOPED_TRACE(focus);
    QSettings().clear();
    const QString source = directory_.filePath(QString("keyboard-%1.json").arg(focus));
    ASSERT_TRUE(QFile::copy(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/polygon/problem-small.json"), source));
    Desktop app(directory_.path());
    app.open(source);
    const auto target = [&]() -> QWidget *
    {
      auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
      click(tabs->tabBar(), tabs->tabBar()->tabRect(focus == 3 ? 1 : 0).center());
      if (focus == 0)
        return widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
      if (focus == 1)
        return widget<QTreeWidget>(app.window, "editorEntityTree");
      if (focus == 2)
        return widget<QLineEdit>(app.window, "editorProblemId");
      return widget<QComboBox>(app.window, "polygonSolverBox");
    };
    const auto initial = bytes(source);
    app.changeWidth(200);
    key(target(), Qt::Key_S, Qt::ControlModifier);
    ASSERT_FALSE(app.workspace->snapshot().documentDirty);
    EXPECT_NE(bytes(source), initial);
    const auto snapshot = app.workspace->snapshot().editableDocument;
    chooseFile({}, true);
    key(target(), Qt::Key_O, Qt::ControlModifier);
    EXPECT_EQ(app.workspace->snapshot().editableDocument, snapshot);
    int starts = 0;
    auto * workspace = app.window.findChild<PolygonWorkspaceWidget *>();
    QObject::connect(workspace, &PolygonWorkspaceWidget::requestStart, &app.window, [&]() { ++starts; });
    key(target(), Qt::Key_Return, Qt::ControlModifier);
    ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().canSave; }));
    EXPECT_EQ(starts, 1);
    const QString solution = directory_.filePath(QString("keyboard-solution-%1.json").arg(focus));
    chooseFile(solution);
    key(target(), Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_TRUE(QFile::exists(solution));
    int undo = 0, redo = 0;
    QObject::connect(widget<QAction>(app.window, "undoDocumentAction"), &QAction::triggered, &app.window, [&]() { ++undo; });
    QObject::connect(widget<QAction>(app.window, "redoDocumentAction"), &QAction::triggered, &app.window, [&]() { ++redo; });
    if (focus == 2)
    {
      // Редактор строки владеет своей текстовой отменой; она не должна менять документ.
      auto * field = qobject_cast<QLineEdit *>(target());
      const auto text = field->text();
      key(field, Qt::Key_End);
      key(field, Qt::Key_F, {}, "f");
      key(field, Qt::Key_Z, Qt::ControlModifier);
      EXPECT_EQ(field->text(), text);
      EXPECT_EQ(undo, 0);
      EXPECT_FALSE(app.workspace->snapshot().documentDirty);
    }
    key(focus == 2 ? widget<PolygonCanvasWidget>(app.window, "polygonCanvas") : target(), Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(undo, 1);
    EXPECT_TRUE(app.workspace->snapshot().documentDirty);
    key(focus == 2 ? widget<PolygonCanvasWidget>(app.window, "polygonCanvas") : target(), Qt::Key_Y, Qt::ControlModifier);
    EXPECT_EQ(redo, 1);
    EXPECT_FALSE(app.workspace->snapshot().documentDirty);
  }
}

/// Отменяет каждую незавершённую стадию линии, дуги и Bézier клавишей, потерей фокуса и деактивацией окна.
TEST_F(GuiExtendedAcceptance, DrawingStagesRollbackWithoutPartialGeometry)
{
  Desktop app(directory_.path());
  createRectangle(app);
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  const auto original = allPoints(*app.workspace->snapshot().editableDocument);
  const auto part = app.workspace->snapshot().editableDocument->parts.front().id;
  const char * tools[] = {"editorLineTool", "editorArcTool", "editorBezierTool"};
  for (int tool = 0; tool < 3; ++tool)
    for (int stage = 0; stage <= tool; ++stage)
      for (int interruption = 0; interruption < 3; ++interruption)
      {
        SCOPED_TRACE(tool);
        SCOPED_TRACE(stage);
        SCOPED_TRACE(interruption);
        selectEntity(app, part);
        action(app.window, "editorHoleTool");
        key(canvas, Qt::Key_F);
        click(canvas, fitted(canvas, 40, 40));
        action(app.window, tools[tool]);
        if (stage >= 1)
          click(canvas, fitted(canvas, 50, 40));
        if (stage >= 2)
          click(canvas, fitted(canvas, 50, 60));
        if (interruption == 0)
          key(canvas, Qt::Key_Escape);
        else if (interruption == 1)
        {
          QFocusEvent event(QEvent::FocusOut);
          QApplication::sendEvent(canvas, &event);
        }
        else
        {
          QEvent event(QEvent::WindowDeactivate);
          QApplication::sendEvent(canvas, &event);
        }
        EXPECT_TRUE(app.workspace->snapshot().editableDocument->parts.front().holes.empty());
        EXPECT_EQ(allPoints(*app.workspace->snapshot().editableDocument), original);
      }
}
} // namespace acceptance
