#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <variant>

#include <aipackaging/editor/polygon_editor_interaction.h>
#include <gtest/gtest.h>
#include <polygondocumentpanel.h>
#include <polygoneditorcanvasinteraction.h>
#include <polygoneditorpanel.h>
#include <polygoneditortoolbar.h>
#include <polygonproblemspanel.h>
#include <polygonrunpanel.h>
#include <polygonstatuspanel.h>

namespace
{
/// Создаёт единственное приложение Qt либо возвращает экземпляр из другого тестового файла.
QApplication * ensurePanelApplication()
{
  static QTemporaryDir settingsDirectory;
  QCoreApplication::setOrganizationName(QStringLiteral("AIPackagingTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AIPackagingGuiPanelTests"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
  if (auto * application = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return application;
  static int argumentCount = 1;
  static char applicationName[] = "aipackaging-panel-tests";
  static char * arguments[] = {applicationName, nullptr};
  static auto application = std::make_unique<QApplication>(argumentCount, arguments);
  return application.get();
}

/// Создаёт идентифицированную цепочку с дугой для проверки выбора и привязок.
aipackaging::editor::EditablePolygonDocument interactiveDocument()
{
  using namespace aipackaging::editor;
  EditablePolygonDocument document;
  document.problemId = "interactive";
  document.sheet.id = {1};
  document.sheet.width = 100.0;
  document.sheet.height = 80.0;
  EditablePart part;
  part.id = {2};
  part.partId = "detail";
  part.outer.emplace();
  part.outer->id = {3};
  part.outer->closed = true;
  part.outer->vertices = {{{4}, 0.0, 0.0}, {{5}, 20.0, 0.0}, {{6}, 20.0, 20.0}, {{7}, 0.0, 20.0}};
  part.outer->segments = {{{8}, EditableSegmentKind::Arc, {{12}, 10.0, 0.0}, {}, {}, false},
                          {{9}, EditableSegmentKind::Line},
                          {{10}, EditableSegmentKind::Line},
                          {{11}, EditableSegmentKind::Line}};
  document.parts.push_back(std::move(part));
  document.restoreNextEntityId(13);
  return document;
}
} // namespace

/// Сверяет дуги Qt с математическими точками и экстремумами, не отражая координаты документа.
TEST(PolygonEditorCanvas, ArcPathPreservesDocumentAngles)
{
  using namespace aipackaging::editor;
  for (const double start : {0.0, 45.0, 90.0, 180.0, 350.0})
    for (const double sweep : {-270.0, -180.0, -90.0, -20.0, 20.0, 90.0, 180.0, 270.0})
    {
      SCOPED_TRACE(::testing::Message() << "start=" << start << " sweep=" << sweep);
      const QPointF center(50.0, 60.0);
      constexpr double RADIUS = 30.0;
      const auto point = [&](double degrees)
      {
        const double angle = degrees * std::numbers::pi / 180.0;
        return center + QPointF(RADIUS * std::cos(angle), RADIUS * std::sin(angle));
      };
      const QPointF first = point(start);
      const QPointF last = point(start + sweep);
      EditablePath source;
      source.id = {3};
      source.vertices = {{{4}, first.x(), first.y()}, {{5}, last.x(), last.y()}};
      source.segments = {{{8}, EditableSegmentKind::Arc, {{12}, center.x(), center.y()}, {}, {}, sweep < 0.0}};
      const auto path = editablePainterPath(source);
      ASSERT_GT(path.elementCount(), 1);
      EXPECT_DOUBLE_EQ(path.elementAt(0).x, first.x());
      EXPECT_DOUBLE_EQ(path.elementAt(0).y, first.y());
      // Qt аппроксимирует дугу кубическими кривыми: это допуск проверки отображения, не нормализации.
      constexpr double DISPLAY_TOLERANCE = 0.02;
      EXPECT_NEAR(path.currentPosition().x(), last.x(), DISPLAY_TOLERANCE);
      EXPECT_NEAR(path.currentPosition().y(), last.y(), DISPLAY_TOLERANCE);
      double minX = std::min(first.x(), last.x());
      double maxX = std::max(first.x(), last.x());
      double minY = std::min(first.y(), last.y());
      double maxY = std::max(first.y(), last.y());
      for (int quadrant = -4; quadrant <= 8; ++quadrant)
      {
        const double angle = quadrant * 90.0;
        if (angle < std::min(start, start + sweep) || angle > std::max(start, start + sweep))
          continue;
        const QPointF extreme = point(angle);
        minX = std::min(minX, extreme.x());
        maxX = std::max(maxX, extreme.x());
        minY = std::min(minY, extreme.y());
        maxY = std::max(maxY, extreme.y());
      }
      const QRectF bounds = path.boundingRect();
      EXPECT_NEAR(bounds.left(), minX, DISPLAY_TOLERANCE);
      EXPECT_NEAR(bounds.right(), maxX, DISPLAY_TOLERANCE);
      EXPECT_NEAR(bounds.top(), minY, DISPLAY_TOLERANCE);
      EXPECT_NEAR(bounds.bottom(), maxY, DISPLAY_TOLERANCE);
      auto document = interactiveDocument();
      document.parts.front().outer = source;
      const auto hit = findEditableEntity(document, point(start + sweep / 2.0), 0.1, {}, {2});
      if (hit)
        EXPECT_EQ(hit->entity, EntityId{8});
      else
        ADD_FAILURE() << "Середина дуги не найдена под указателем";
      for (int index = 1; index < path.elementCount(); ++index)
      {
        const auto element = path.elementAt(index);
        if (element.isLineTo())
        {
          // Возможна лишь малая поправка Qt к аппроксимированному началу, но не соединение с отражённой точкой.
          EXPECT_NEAR(element.x, first.x(), DISPLAY_TOLERANCE);
          EXPECT_NEAR(element.y, first.y(), DISPLAY_TOLERANCE);
        }
      }
    }
}

/// Проверяет экранный приоритет точки, устойчивую привязку и выбор рамкой.
TEST(PolygonEditorPanel, ClearsPropertiesAcrossDocumentsAndMultipleSelection)
{
  ensurePanelApplication();
  PolygonEditorPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.canEdit = true;
  snapshot.documentIdentity = 1;
  snapshot.editableDocument = std::make_shared<const aipackaging::editor::EditablePolygonDocument>(interactiveDocument());
  panel.present(snapshot);
  panel.selectEntities({2});
  auto * name = panel.findChild<QLineEdit *>("editorPartId");
  auto * apply = panel.findChild<QPushButton *>("applyPartButton");
  ASSERT_NE(name, nullptr);
  ASSERT_NE(apply, nullptr);
  EXPECT_EQ(name->text(), QStringLiteral("detail"));
  EXPECT_TRUE(apply->isEnabled());
  ++snapshot.documentIdentity;
  panel.present(snapshot);
  EXPECT_TRUE(name->text().isEmpty());
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_TRUE(panel.findChild<QPushButton *>("addPartButton")->isEnabled());
  panel.selectEntities({4, 5});
  EXPECT_TRUE(name->text().isEmpty());
  EXPECT_FALSE(apply->isEnabled());
  panel.resize(340, 550);
  panel.show();
  QApplication::processEvents();
  ASSERT_NE(panel.findChild<QSplitter *>("editorSectionsSplitter"), nullptr);
  EXPECT_GE(panel.findChild<QScrollArea *>()->height(), 180);
}

/// Проверяет отдельную доступную строку отказа при сохранённой прежней модели.
TEST(PolygonRunPanel, ShowsModelFailureSeparately)
{
  ensurePanelApplication();
  PolygonRunPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.modelReady = true;
  snapshot.modelState = PolygonModelState::Error;
  snapshot.modelId = std::string(100, 'x');
  snapshot.modelStatusText = "В каталоге модели отсутствует файл метаданных";
  panel.present(snapshot);
  auto * message = panel.findChild<QLabel *>("polygonModelMessage");
  ASSERT_NE(message, nullptr);
  EXPECT_TRUE(message->wordWrap());
  EXPECT_FALSE(message->isHidden());
  EXPECT_TRUE(message->text().contains(QStringLiteral("прежняя проверенная модель")));
  EXPECT_TRUE(message->text().contains(QStringLiteral("отсутствует")));
  EXPECT_EQ(message->focusPolicy(), Qt::StrongFocus);
}

/// Проверяет экранный приоритет точки, устойчивую привязку и выбор рамкой.
TEST(PolygonCanvasInteraction, SelectsAndSnapsEditorEntitiesDeterministically)
{
  using namespace aipackaging::editor;
  const EditablePolygonDocument document = interactiveDocument();
  const auto vertex = findEditableEntity(document, QPointF(0.1, 0.1), 1.0, {}, {2});
  ASSERT_TRUE(vertex.has_value());
  EXPECT_EQ(vertex->entity, EntityId{4});
  EXPECT_EQ(vertex->kind, EditorEntityKind::Point);

  PolygonCanvasSnapSettings settings;
  settings.geometryEnabled = true;
  settings.gridEnabled = true;
  settings.gridStepMm = 10.0;
  const auto endpoint = snapEditablePoint(document, QPointF(0.2, 0.2), 1.0, settings, {2}, {}, {4});
  EXPECT_EQ(endpoint.kind, PolygonSnapKind::ClosingVertex);
  EXPECT_EQ(endpoint.entity, EntityId{4});
  const auto grid = snapEditablePoint(document, QPointF(0.2, 0.2), 1.0, settings, {2}, {{4}}, {});
  EXPECT_EQ(grid.kind, PolygonSnapKind::Grid);

  const auto enclosed = findEditableEntities(document, QRectF(-1.0, -11.0, 22.0, 32.0), false, {2});
  EXPECT_NE(std::find(enclosed.begin(), enclosed.end(), EntityId{3}), enclosed.end());
  EXPECT_NE(std::find(enclosed.begin(), enclosed.end(), EntityId{8}), enclosed.end());
}

/// Проверяет совпадение временного индекса с последовательным поиском и повторное использование одной редакции.
TEST(PolygonCanvasInteraction, SpatialIndexPreservesSelectionAndSnappingSemantics)
{
  using namespace aipackaging::editor;
  auto document = std::make_shared<EditablePolygonDocument>(interactiveDocument());
  PolygonCanvasSpatialIndex index;
  index.rebuild(document);
  EXPECT_EQ(index.document(), document);

  const QPointF point(0.2, 0.2);
  const auto sequentialHit = findEditableEntity(*document, point, 1.0, {}, {2});
  const auto indexedHit = index.find(point, 1.0, {}, {2});
  ASSERT_TRUE(sequentialHit.has_value());
  ASSERT_TRUE(indexedHit.has_value());
  EXPECT_EQ(indexedHit->entity, sequentialHit->entity);
  EXPECT_EQ(indexedHit->kind, sequentialHit->kind);

  const QRectF rectangle(-1.0, -11.0, 22.0, 32.0);
  EXPECT_EQ(index.findInRectangle(rectangle, false, {2}), findEditableEntities(*document, rectangle, false, {2}));

  PolygonCanvasSnapSettings settings;
  settings.geometryEnabled = true;
  settings.gridEnabled = true;
  settings.gridStepMm = 10.0;
  const auto sequentialSnap = snapEditablePoint(*document, point, 1.0, settings, {2}, {}, {4});
  const auto indexedSnap = index.snap(point, 1.0, settings, {2}, {}, {4});
  EXPECT_EQ(indexedSnap.kind, sequentialSnap.kind);
  EXPECT_EQ(indexedSnap.entity, sequentialSnap.entity);
  EXPECT_EQ(indexedSnap.point, sequentialSnap.point);
}

/// Габарит диагонали не считается пересечением рамки с самим сегментом.
TEST(PolygonCanvasInteraction, CrossingRectangleChecksSegmentGeometry)
{
  using namespace aipackaging::editor;
  auto document = std::make_shared<EditablePolygonDocument>(interactiveDocument());
  auto & path = *document->parts.front().outer;
  path.closed = false;
  path.vertices = {{{4}, 0.0, 0.0}, {{5}, 10.0, 10.0}};
  path.segments = {{{8}, EditableSegmentKind::Line}};
  PolygonCanvasSpatialIndex index;
  index.rebuild(document);
  EXPECT_TRUE(index.findInRectangle(QRectF(0, 8, 2, 2), true, {2}).empty());
  const auto crossing = index.findInRectangle(QRectF(4, 4, 2, 2), true, {2});
  EXPECT_NE(std::find(crossing.begin(), crossing.end(), EntityId{8}), crossing.end());
  EXPECT_TRUE(index.findInRectangle(QRectF(4, 4, 2, 2), false, {2}).empty());

  path.vertices.back().y = 0.0;
  path.segments.front() = {{8}, EditableSegmentKind::CubicBezier, {}, {{12}, 0.0, 10.0}, {{13}, 10.0, 10.0}};
  index.rebuild(document);
  const auto outsideCurve = index.findInRectangle(QRectF(4, 2, 2, 2), true, {2});
  EXPECT_EQ(std::find(outsideCurve.begin(), outsideCurve.end(), EntityId{8}), outsideCurve.end());
  const auto insideCurve = index.findInRectangle(QRectF(4, 7, 2, 1), true, {2});
  EXPECT_NE(std::find(insideCurve.begin(), insideCurve.end(), EntityId{8}), insideCurve.end());
}

/// Проверяет значения сетки по умолчанию и их сохранение в устойчивых ключах Qt.
TEST(PolygonEditorToolBar, PersistsGridAndSnapSettings)
{
  ensurePanelApplication();
  QSettings settings;
  settings.remove(QStringLiteral("editor"));
  {
    PolygonEditorToolBar toolbar;
    const PolygonCanvasSnapSettings initial = toolbar.snapSettings();
    EXPECT_TRUE(initial.gridVisible);
    EXPECT_DOUBLE_EQ(initial.gridStepMm, 10.0);
    EXPECT_TRUE(initial.geometryEnabled);
    EXPECT_FALSE(initial.gridEnabled);
    auto * gridSnap = toolbar.findChild<QCheckBox *>(QStringLiteral("editorGridSnap"));
    ASSERT_NE(gridSnap, nullptr);
    gridSnap->setChecked(true);
  }
  settings.sync();
  EXPECT_TRUE(settings.value(QStringLiteral("editor/snapGrid")).toBool());
  PolygonEditorToolBar restored;
  EXPECT_TRUE(restored.snapSettings().gridEnabled);
  settings.remove(QStringLiteral("editor"));
}

/// Проверяет безопасный возврат к значениям по умолчанию после повреждения настроек сетки.
TEST(PolygonEditorToolBar, RejectsCorruptedGridSettings)
{
  ensurePanelApplication();
  QSettings settings;
  settings.remove(QStringLiteral("editor"));
  settings.setValue(QStringLiteral("editor/gridStepMm"), QStringLiteral("nan"));
  settings.sync();

  PolygonEditorToolBar toolbar;
  EXPECT_DOUBLE_EQ(toolbar.snapSettings().gridStepMm, 10.0);
  settings.remove(QStringLiteral("editor"));
}

/// Проверяет выбор проблемы по связанному идентификатору без применения изменения.
TEST(PolygonProblemsPanel, SelectsDiagnosticByRelatedEntity)
{
  ensurePanelApplication();
  PolygonProblemsPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.documentDiagnostics.push_back({aipackaging::editor::DocumentDiagnosticCode::DegenerateSegment,
                                          aipackaging::editor::DiagnosticSeverity::Error,
                                          {7},
                                          "Сегмент вырожден",
                                          {{8}, {9}}});
  panel.present(snapshot);
  panel.selectEntities({9});
  const auto * list = panel.findChild<QListWidget *>(QStringLiteral("polygonProblemsList"));
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(list->currentRow(), 0);
}

/// Проверяет независимую публикацию документа и выбор строки дерева деталей.
TEST(PolygonDocumentPanel, PresentsDocumentAndSelection)
{
  ensurePanelApplication();
  PolygonDocumentPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.problemId = "panel-job";
  snapshot.document.sheetWidth = 120.0;
  snapshot.document.sheetHeight = 80.0;
  PolygonPartSummary part;
  part.id = "bracket";
  part.quantity = 2;
  part.allowedRotations = {0, 90};
  part.width = 12.5;
  part.height = 8.0;
  snapshot.document.parts.push_back(part);
  panel.present(snapshot);

  auto * tree = panel.findChild<QTreeWidget *>(QStringLiteral("polygonPartTree"));
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("bracket"));
  bool selected = false;
  QObject::connect(&panel, &PolygonDocumentPanel::partSelected, [&selected](const QString & partId, std::uint32_t instance)
                   { selected = partId == QStringLiteral("bracket") && instance == 0; });
  tree->setCurrentItem(tree->topLevelItem(0));
  EXPECT_TRUE(selected);
}

/// Проверяет режимы, полный диапазон начального значения и пересылку команд панели запуска.
TEST(PolygonRunPanel, BuildsRequestsAndForwardsActions)
{
  ensurePanelApplication();
  PolygonRunPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Ready;
  snapshot.canRun = true;
  snapshot.canOpen = true;
  panel.present(snapshot);

  auto * modes = panel.findChild<QComboBox *>(QStringLiteral("polygonSolverBox"));
  auto * seed = panel.findChild<QLineEdit *>(QStringLiteral("polygonSeedEdit"));
  auto * start = panel.findChild<QPushButton *>(QStringLiteral("polygonStartButton"));
  ASSERT_NE(modes, nullptr);
  ASSERT_NE(seed, nullptr);
  ASSERT_NE(start, nullptr);
  EXPECT_EQ(panel.solverConfig().algorithm, BaselineAlgorithm::AreaLeftBottom);
  modes->setCurrentIndex(1);
  EXPECT_EQ(panel.solverConfig().algorithm, BaselineAlgorithm::Beam);
  seed->setText(QString::number(std::numeric_limits<qulonglong>::max()));
  EXPECT_EQ(panel.solverConfig().seed, std::numeric_limits<std::uint64_t>::max());

  bool started = false;
  QObject::connect(&panel, &PolygonRunPanel::requestStart, [&started]() { started = true; });
  start->click();
  EXPECT_TRUE(started);
}

/// Проверяет модель, неразмещённые экземпляры и запрет нейросетевого запуска без модели.
TEST(PolygonRunPanel, PresentsModelAndUnplacedInstances)
{
  ensurePanelApplication();
  PolygonRunPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Ready;
  snapshot.canRun = true;
  snapshot.canLoadModel = true;
  snapshot.unplacedInstances = {"bracket #1"};
  panel.present(snapshot);
  auto * modes = panel.findChild<QComboBox *>(QStringLiteral("polygonSolverBox"));
  auto * start = panel.findChild<QPushButton *>(QStringLiteral("polygonStartButton"));
  auto * unplaced = panel.findChild<QListWidget *>(QStringLiteral("polygonUnplacedInstances"));
  ASSERT_NE(modes, nullptr);
  ASSERT_NE(start, nullptr);
  ASSERT_NE(unplaced, nullptr);
  modes->setCurrentIndex(2);
  EXPECT_FALSE(start->isEnabled());
  ASSERT_EQ(unplaced->count(), 1);
  EXPECT_EQ(unplaced->item(0)->text(), QStringLiteral("bracket #1"));

  snapshot.modelReady = true;
  snapshot.modelState = PolygonModelState::Ready;
  snapshot.modelId = "policy";
  snapshot.modelSha256 = "0123456789abcdef";
  panel.present(snapshot);
  EXPECT_TRUE(start->isEnabled());
  const auto * label = panel.findChild<QLabel *>(QStringLiteral("polygonModelStatus"));
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains(QStringLiteral("0123456789ab")));
}

/// Проверяет независимую публикацию частичного хода и метрик результата.
TEST(PolygonStatusPanel, PreservesPartialProgressAndMetrics)
{
  ensurePanelApplication();
  PolygonStatusPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Completed;
  snapshot.partial = true;
  snapshot.statusText = "Исчерпан бюджет";
  snapshot.progress = {NestingProgressStage::ExpandedStates, 25, 100, 25};
  snapshot.objective.placedParts = 1;
  snapshot.objective.totalParts = 4;
  panel.present(snapshot);

  const auto * status = panel.findChild<QLabel *>(QStringLiteral("polygonStatus"));
  const auto * stage = panel.findChild<QLabel *>(QStringLiteral("polygonProgressStage"));
  const auto * progress = panel.findChild<QProgressBar *>(QStringLiteral("polygonProgress"));
  ASSERT_NE(status, nullptr);
  ASSERT_NE(stage, nullptr);
  ASSERT_NE(progress, nullptr);
  EXPECT_TRUE(status->styleSheet().contains(QStringLiteral("font-weight")));
  EXPECT_TRUE(stage->text().contains(QStringLiteral("лучевой")));
  EXPECT_EQ(progress->value(), 250);
}

/// Проверяет показ предметных диагностик некорректного редактируемого документа.
TEST(PolygonStatusPanel, ShowsEditableDocumentDiagnostics)
{
  ensurePanelApplication();
  PolygonStatusPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.statusText = "Черновик загружен";
  snapshot.documentDiagnostics.push_back({aipackaging::editor::DocumentDiagnosticCode::OpenPath,
                                          aipackaging::editor::DiagnosticSeverity::Error,
                                          {},
                                          "Внешний контур детали открыт"});
  panel.present(snapshot);

  const auto * status = panel.findChild<QLabel *>(QStringLiteral("polygonStatus"));
  ASSERT_NE(status, nullptr);
  EXPECT_TRUE(status->text().contains(QStringLiteral("Проблемы документа: 1")));
  EXPECT_TRUE(status->text().contains(QStringLiteral("Подробности на вкладке «Проблемы»")));
}

/// Проверяет, что числовая панель публикует команды, а не изменяет снимок напрямую.
TEST(PolygonEditorPanel, BuildsDocumentAndPartCommands)
{
  ensurePanelApplication();
  PolygonEditorPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.canEdit = true;
  snapshot.documentValid = false;
  auto document = std::make_shared<aipackaging::editor::EditablePolygonDocument>();
  document->problemId = "draft";
  document->sheet.width = 100.0;
  document->sheet.height = 80.0;
  snapshot.editableDocument = std::move(document);
  panel.present(snapshot);

  std::vector<aipackaging::editor::EditorCommandBatch> commands;
  QObject::connect(&panel, &PolygonEditorPanel::editRequested,
                   [&commands](const aipackaging::editor::EditorCommandBatch & batch) { commands.push_back(batch); });
  auto * addPart = panel.findChild<QPushButton *>(QStringLiteral("addPartButton"));
  auto * applyDocument = panel.findChild<QPushButton *>(QStringLiteral("applyDocumentPropertiesButton"));
  ASSERT_NE(addPart, nullptr);
  ASSERT_NE(applyDocument, nullptr);
  addPart->click();
  applyDocument->click();
  ASSERT_EQ(commands.size(), 2U);
  ASSERT_EQ(commands.front().commands.size(), 1U);
  EXPECT_TRUE(std::holds_alternative<aipackaging::editor::AddPartCommand>(commands.front().commands.front()));
  EXPECT_EQ(commands.back().commands.size(), 3U);
}

/// Проверяет передачу направления дуги из числовой формы выбранного контура.
TEST(PolygonEditorPanel, BuildsDirectedArcCommand)
{
  ensurePanelApplication();
  PolygonEditorPanel panel;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.canEdit = true;
  auto document = std::make_shared<aipackaging::editor::EditablePolygonDocument>();
  aipackaging::editor::EditablePart part;
  part.id = {1};
  part.partId = "arc";
  part.outer.emplace();
  part.outer->id = {2};
  part.outer->vertices.push_back({{3}, 0.0, 0.0});
  document->parts.push_back(std::move(part));
  document->restoreNextEntityId(4);
  snapshot.editableDocument = std::move(document);
  panel.present(snapshot);

  auto * tree = panel.findChild<QTreeWidget *>(QStringLiteral("editorEntityTree"));
  auto * kind = panel.findChild<QComboBox *>(QStringLiteral("editorSegmentKind"));
  auto * clockwise = panel.findChild<QCheckBox *>(QStringLiteral("editorArcClockwise"));
  auto * append = panel.findChild<QPushButton *>(QStringLiteral("appendSegmentButton"));
  ASSERT_NE(tree, nullptr);
  ASSERT_NE(kind, nullptr);
  ASSERT_NE(clockwise, nullptr);
  ASSERT_NE(append, nullptr);
  tree->setCurrentItem(tree->topLevelItem(0)->child(0));
  kind->setCurrentIndex(static_cast<int>(aipackaging::editor::EditableSegmentKind::Arc));
  clockwise->setChecked(true);

  std::optional<aipackaging::editor::EditorCommandBatch> emitted;
  QObject::connect(&panel, &PolygonEditorPanel::editRequested,
                   [&emitted](const aipackaging::editor::EditorCommandBatch & batch) { emitted = batch; });
  append->click();
  ASSERT_TRUE(emitted.has_value());
  const auto & command = std::get<aipackaging::editor::AppendSegmentCommand>(emitted->commands.front());
  EXPECT_TRUE(command.segment.clockwise);
}
