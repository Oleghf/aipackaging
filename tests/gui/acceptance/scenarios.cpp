#include <iostream>
#include <QAbstractButton>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFocusEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QWizard>

#include <gtest/gtest.h>
#include <polygoncanvaswidget.h>
#include <polygondxfimportwizard.h>

#include "support.h"

namespace acceptance
{
/// Готовит отдельные файлы каждой проверки и сбрасывает только тестовые настройки.
class GuiAcceptance : public testing::Test
{
protected:
  QTemporaryDir directory_;
  /// Создаёт чистое изолированное окружение перед каждым пользовательским сценарием.
  void SetUp() override
  {
    ASSERT_TRUE(directory_.isValid());
    QSettings().clear();
  }
  /// Копирует поставляемую задачу в каталог с именем вне системной кодовой страницы.
  QString problem() const
  {
    const QString path = directory_.filePath(QStringLiteral("задача 漢字 A.json"));
    if (!QFile::copy(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/polygon/problem-small.json"), path))
      throw std::runtime_error("Не удалось подготовить задачу приёмки");
    return path;
  }
};

/// Сохраняет через окно под новым именем, затем подтверждает неизменность исходника и новый путь.
TEST_F(GuiAcceptance, SaveAsThenImmediateSaveKeepsOriginal)
{
  const QString original = problem();
  const QByteArray initial = bytes(original);
  Desktop app(directory_.path());
  app.open(original);
  ASSERT_TRUE(app.workspace->snapshot().documentValid);
  const QString destination = directory_.filePath(QStringLiteral("задача 漢字 B.json"));
  chooseFile(destination);
  action(app.window, "saveProblemAsAction");
  ASSERT_TRUE(QFile::exists(destination));
  key(&app.window, Qt::Key_S, Qt::ControlModifier);
  EXPECT_EQ(app.workspace->snapshot().document.sourceIdentifier, destination.toStdString());
  EXPECT_EQ(bytes(original), initial);
  const QByteArray saved = bytes(destination);
  app.changeWidth(123.0);
  ASSERT_TRUE(app.workspace->snapshot().documentDirty);
  key(&app.window, Qt::Key_S, Qt::ControlModifier);
  EXPECT_NE(bytes(destination), saved);
  EXPECT_EQ(bytes(original), initial);
  EXPECT_FALSE(app.workspace->snapshot().documentDirty);
}

/// Отмена диалога не меняет путь, историю или файл, а повреждённое открытие сохраняет задачу.
TEST_F(GuiAcceptance, CancelSaveAndFailedOpenPreserveDocument)
{
  Desktop app(directory_.path());
  const QString original = problem();
  app.open(original);
  const auto snapshot = app.workspace->snapshot();
  chooseFile({}, true);
  action(app.window, "saveProblemAsAction");
  EXPECT_EQ(app.workspace->snapshot().document.sourceIdentifier, snapshot.document.sourceIdentifier);
  const QString corrupt = directory_.filePath("corrupt.json");
  QFile file(corrupt);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("{broken");
  file.close();
  app.open(corrupt);
  EXPECT_EQ(app.workspace->snapshot().editableDocument, snapshot.editableDocument);
  EXPECT_TRUE(app.workspace->snapshot().documentValid);
}

/// Запускает базовый раскрой из окна, сохраняет решение и проверяет отдельным процессом CLI.
TEST_F(GuiAcceptance, FastSolveSavedFromWindowPassesCli)
{
  Desktop app(directory_.path());
  const QString source = problem();
  app.open(source);
  key(&app.window, Qt::Key_Return, Qt::ControlModifier);
  ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().canSave; }));
  const QString solution = directory_.filePath(QStringLiteral("решение 漢字.json"));
  chooseFile(solution);
  key(&app.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
  ASSERT_TRUE(QFile::exists(solution));
  QProcess cli;
  cli.start(QStringLiteral(AIPACKAGING_CLI_PATH), {"validate", "--problem", source, "--solution", solution});
  ASSERT_TRUE(cli.waitForFinished(30000));
  EXPECT_EQ(cli.exitCode(), 0) << cli.readAllStandardError().toStdString();
  app.changeWidth(125.0);
  EXPECT_FALSE(app.workspace->snapshot().canSave);
  EXPECT_TRUE(app.workspace->snapshot().solutionStale);
  EXPECT_TRUE(app.workspace->snapshot().canUndo);
}

/// Создаёт задачу формой и замкнутый прямоугольник щелчками настоящего полотна.
TEST_F(GuiAcceptance, CreateDrawSaveAndReopen)
{
  Desktop app(directory_.path());
  whenModal(
    [](QWidget * dialog)
    {
      enter(widget<QLineEdit>(*dialog, "newProblemId"), "acceptance-created");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetWidth"), "100");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetHeight"), "80");
      click(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok));
    });
  action(app.window, "createDocumentAction");
  ASSERT_TRUE(app.workspace->snapshot().hasDocument);
  EXPECT_FALSE(app.workspace->snapshot().documentValid);
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  click(widget<QWidget>(app.window, "addPartButton"));
  auto * tree = widget<QTreeWidget>(app.window, "editorEntityTree");
  ASSERT_EQ(tree->topLevelItemCount(), 1);
  click(tree->viewport(), tree->visualItemRect(tree->topLevelItem(0)).center());
  action(app.window, "editorOuterTool");
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  const double scale = std::min((canvas->width() - 48.0) / 100.0, (canvas->height() - 48.0) / 80.0);
  const auto point = [&](double x, double y)
  {
    return QPoint(qRound(canvas->width() / 2.0 + (x - 50.0) * scale), qRound(canvas->height() / 2.0 - (y - 40.0) * scale));
  };
  for (const auto & position : {point(10, 10), point(30, 10), point(30, 30), point(10, 30), point(10, 10)})
    click(canvas, position);
  ASSERT_TRUE(app.workspace->snapshot().documentValid) << app.workspace->snapshot().statusText;
  const auto outer = app.workspace->snapshot().editableDocument->parts.front().outer;
  ASSERT_TRUE(outer.has_value());
  if (outer)
    ASSERT_EQ(outer->vertices.size(), 4U);
  const QString saved = directory_.filePath("created.json");
  chooseFile(saved);
  key(&app.window, Qt::Key_S, Qt::ControlModifier);
  ASSERT_TRUE(QFile::exists(saved));
  app.open(saved);
  EXPECT_TRUE(app.workspace->snapshot().documentValid);
  EXPECT_FALSE(app.workspace->snapshot().documentDirty);
  key(&app.window, Qt::Key_Return, Qt::ControlModifier);
  ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().canSave; }));
  const QString solution = directory_.filePath("created-solution.json");
  chooseFile(solution);
  key(&app.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
  QProcess cli;
  cli.start(QStringLiteral(AIPACKAGING_CLI_PATH), {"validate", "--problem", saved, "--solution", solution});
  ASSERT_TRUE(cli.waitForFinished(30000));
  EXPECT_EQ(cli.exitCode(), 0) << cli.readAllStandardError().toStdString();
}

/// Проходит страницы мастера реального DXF и принимает построенный документ.
TEST_F(GuiAcceptance, DxfExamplesThroughAllWizardPages)
{
  for (const QString & example : {QStringLiteral("square-mm"), QStringLiteral("bulge-inch"), QStringLiteral("spline-and-note")})
  {
    SCOPED_TRACE(example.toStdString());
    QSettings().clear();
    Desktop app(directory_.path());
    const QString input = directory_.filePath(QStringLiteral("деталь 漢字 ") + example + ".dxf");
    ASSERT_TRUE(QFile::copy(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/dxf/") + example + ".dxf", input));
    chooseFile(input);
    action(app.window, "importDxfAction");
    ASSERT_TRUE(waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Ready; }));
    auto * wizard = widget<PolygonDxfImportWizard>(app.window, "polygonDxfImportWizard");
    for (int page = 0; page < 4; ++page)
    {
      SCOPED_TRACE(page);
      EXPECT_EQ(wizard->currentId(), page);
      if (page == 1)
      {
        auto * ignore = widget<QCheckBox>(*wizard, "dxfIgnoreUnsupported");
        if (!ignore->isChecked())
          key(ignore, Qt::Key_Space);
      }
      key(wizard->button(QWizard::NextButton), Qt::Key_Space);
    }
    ASSERT_EQ(wizard->currentId(), 4);
    key(wizard->button(QWizard::FinishButton), Qt::Key_Space);
    ASSERT_TRUE(waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Completed; }));
    key(wizard->button(QWizard::FinishButton), Qt::Key_Space);
    ASSERT_TRUE(app.workspace->snapshot().hasDocument);
    EXPECT_EQ(app.workspace->snapshot().documentSource, PolygonDocumentSource::Imported);
    if (example == "spline-and-note")
    {
      EXPECT_FALSE(app.workspace->snapshot().documentValid);
      const QString saved = directory_.filePath(example + ".aipdraft.json");
      chooseFile(saved);
      key(&app.window, Qt::Key_S, Qt::ControlModifier);
      EXPECT_TRUE(QFile::exists(saved));
      continue;
    }
    ASSERT_TRUE(app.workspace->snapshot().documentValid);
    const double width = app.workspace->snapshot().editableDocument->sheet.width;
    app.changeWidth(width + 10);
    const QString saved = directory_.filePath(example + ".json");
    chooseFile(saved);
    key(&app.window, Qt::Key_S, Qt::ControlModifier);
    ASSERT_TRUE(QFile::exists(saved));
    app.open(saved);
    EXPECT_TRUE(app.workspace->snapshot().documentValid);
  }
}

/// Не позволяет принять программный щелчок по кнопке вне видимой области мастера.
TEST_F(GuiAcceptance, DxfNavigationButtonIsReachable)
{
  Desktop app(directory_.path());
  chooseFile(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/dxf/square-mm.dxf"));
  action(app.window, "importDxfAction");
  ASSERT_TRUE(waitUntil([&]() { return app.importer->snapshot().state == PolygonImportState::Ready; }));
  auto * wizard = widget<PolygonDxfImportWizard>(app.window, "polygonDxfImportWizard");
  auto * next = wizard->button(QWizard::NextButton);
  QCoreApplication::processEvents();
  EXPECT_TRUE(next->visibleRegion().contains(next->rect().center()))
    << "GUI-A1-01: кнопка Далее находится вне видимой области мастера offscreen";
  QWizard reference;
  reference.setMinimumSize(900, 650);
  reference.setPage(0, new QWizardPage);
  reference.setPage(1, new QWizardPage);
  reference.show();
  QCoreApplication::processEvents();
  auto * referenceNext = reference.button(QWizard::NextButton);
  std::cout << "Доступность кнопки пустого мастера Qt: "
            << referenceNext->visibleRegion().contains(referenceNext->rect().center()) << '\n';
}

/// Замыкает две полуокружности через реальные контроллеры и отменяет стадии ввода отверстия.
TEST_F(GuiAcceptance, CurvedClosureAndStageCancellation)
{
  Desktop app(directory_.path());
  whenModal(
    [](QWidget * dialog)
    {
      enter(widget<QLineEdit>(*dialog, "newProblemId"), "curved-acceptance");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetWidth"), "100");
      enter(widget<QDoubleSpinBox>(*dialog, "newSheetHeight"), "80");
      click(dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok));
    });
  action(app.window, "createDocumentAction");
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  click(widget<QWidget>(app.window, "addPartButton"));
  auto * tree = widget<QTreeWidget>(app.window, "editorEntityTree");
  click(tree->viewport(), tree->visualItemRect(tree->topLevelItem(0)).center());
  auto * canvas = widget<PolygonCanvasWidget>(app.window, "polygonCanvas");
  const double scale = std::min((canvas->width() - 48.0) / 100.0, (canvas->height() - 48.0) / 80.0);
  const auto point = [&](double x, double y)
  {
    return QPoint(qRound(canvas->width() / 2.0 + (x - 50.0) * scale), qRound(canvas->height() / 2.0 - (y - 40.0) * scale));
  };
  action(app.window, "editorOuterTool");
  click(canvas, point(20, 40));
  action(app.window, "editorArcTool");
  click(canvas, point(50, 40));
  click(canvas, point(80, 40));
  click(canvas, point(50, 40));
  click(canvas, point(20, 40));
  ASSERT_TRUE(app.workspace->snapshot().documentValid) << app.workspace->snapshot().statusText;
  const auto & outer = *app.workspace->snapshot().editableDocument->parts.front().outer;
  EXPECT_TRUE(outer.closed);
  EXPECT_EQ(outer.vertices.size(), 2U);
  EXPECT_EQ(outer.segments.size(), 2U);
  // Каждую незавершённую стадию проверяем и клавиатурой, и потерей фокуса.
  for (const bool focusLoss : {false, true})
    for (int stage = 0; stage < 3; ++stage)
    {
      click(tree->viewport(), tree->visualItemRect(tree->topLevelItem(0)).center());
      action(app.window, "editorHoleTool");
      click(canvas, point(40, 40));
      action(app.window, "editorBezierTool");
      if (stage >= 1)
        click(canvas, point(40, 50));
      if (stage >= 2)
        click(canvas, point(60, 50));
      if (focusLoss)
      {
        QFocusEvent event(QEvent::FocusOut, Qt::OtherFocusReason);
        QApplication::sendEvent(canvas, &event);
      }
      else
        key(canvas, Qt::Key_Escape);
      EXPECT_TRUE(app.workspace->snapshot().editableDocument->parts.front().holes.empty());
    }
}

/// Проверяет геометрическую достижимость полей и клавиатурный обход компактного окна.
TEST_F(GuiAcceptance, CompactLayoutAndTabNavigation)
{
  Desktop app(directory_.path());
  app.open(problem());
  EXPECT_EQ(app.window.size(), QSize(1280, 720));
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  for (int page = 0; page < tabs->count(); ++page)
  {
    click(tabs->tabBar(), tabs->tabBar()->tabRect(page).center());
    EXPECT_TRUE(app.window.rect().contains(tabs->mapTo(&app.window, tabs->rect().center())));
  }
  click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  auto * field = widget<QLineEdit>(app.window, "editorProblemId");
  field->setFocus();
  key(field, Qt::Key_Tab);
  QWidget * next = QApplication::focusWidget();
  ASSERT_NE(next, nullptr);
  EXPECT_NE(next, field);
  key(next, Qt::Key_Backtab, Qt::ShiftModifier);
  EXPECT_EQ(QApplication::focusWidget(), field);
}

/// Сохраняет открытый контур как черновик, не разрешая запуск некорректного снимка.
TEST_F(GuiAcceptance, InvalidDraftCanBeSavedButNotSolved)
{
  Desktop app(directory_.path());
  const QString path = directory_.filePath(QStringLiteral("черновик 漢字.aipdraft.json"));
  ASSERT_TRUE(QFile::copy(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/examples/editor/demo-open-path.aipdraft.json"), path));
  app.open(path);
  const auto snapshot = app.workspace->snapshot();
  ASSERT_TRUE(snapshot.hasDocument);
  EXPECT_FALSE(snapshot.documentValid);
  EXPECT_FALSE(snapshot.canRun);
  const QString saved = directory_.filePath(QStringLiteral("сохранённый черновик 漢字.aipdraft.json"));
  chooseFile(saved);
  key(&app.window, Qt::Key_S, Qt::ControlModifier);
  EXPECT_TRUE(bytes(saved).contains("aipackaging.polygon_draft"));
}

/// В дочернем процессе изменяет документ через форму либо принимает карточку восстановления.
int recoveryWorker(const QStringList & arguments)
{
  const qsizetype offset = arguments.indexOf(QStringLiteral("--recovery-worker"));
  if (offset < 0 || arguments.size() < offset + 4)
    return 2;
  const QString & mode = arguments[offset + 1];
  const QString & root = arguments[offset + 2];
  Desktop app(root);
  if (mode == "write")
  {
    app.open(arguments[offset + 3]);
    app.changeWidth(321.0);
    if (!waitUntil([&]() { return QFile::exists(root + "/active.aipdraft.json"); }))
      return 3;
    app.drafts->flush();
    std::cout << "READY\n" << std::flush;
    return QApplication::exec();
  }
  click(widget<QWidget>(app.window, "restoreRecoveryButton"));
  if (!app.workspace->snapshot().documentDirty || !app.workspace->snapshot().editableDocument ||
      app.workspace->snapshot().editableDocument->sheet.width != 321.0)
    return 4;
  chooseFile(arguments[offset + 3]);
  action(app.window, "saveProblemAsAction");
  app.drafts->flush();
  return QFile::exists(arguments[offset + 3]) && !QFile::exists(root + "/active.aipdraft.json") ? 0 : 5;
}

/// Аварийно завершает только созданный тестом процесс после подтверждённой записи и восстанавливает другим.
TEST_F(GuiAcceptance, CrashAfterAutosaveRestoresInFreshProcess)
{
  const QString source = problem();
  QProcess writer;
  writer.start(QCoreApplication::applicationFilePath(), {"--recovery-worker", "write", directory_.path(), source});
  ASSERT_TRUE(writer.waitForStarted());
  QByteArray output;
  const bool ready = waitUntil(
    [&]()
    {
      output += writer.readAllStandardOutput();
      return output.contains("READY");
    },
    15000);
  writer.kill();
  ASSERT_TRUE(writer.waitForFinished(10000));
  ASSERT_TRUE(ready) << writer.readAllStandardError().toStdString();
  ASSERT_TRUE(QFile::exists(directory_.filePath("active.aipdraft.json")));
  const QString restored = directory_.filePath(QStringLiteral("восстановлено 漢字.json"));
  QProcess reader;
  reader.start(QCoreApplication::applicationFilePath(), {"--recovery-worker", "restore", directory_.path(), restored});
  ASSERT_TRUE(reader.waitForFinished(15000));
  EXPECT_EQ(reader.exitCode(), 0) << reader.readAllStandardError().toStdString();
  EXPECT_TRUE(QFile::exists(restored));
}

/// Возврат через клавиатуру к чистой точке удаляет только записанный текущим документом черновик.
TEST_F(GuiAcceptance, UndoToCleanRemovesRecovery)
{
  Desktop app(directory_.path());
  app.open(problem());
  app.changeWidth(200.0);
  ASSERT_TRUE(waitUntil([&]() { return QFile::exists(directory_.filePath("active.aipdraft.json")); }));
  ASSERT_TRUE(app.workspace->snapshot().canUndo);
  widget<PolygonCanvasWidget>(app.window, "polygonCanvas")->setFocus();
  key(&app.window, Qt::Key_Z, Qt::ControlModifier);
  ASSERT_FALSE(app.workspace->snapshot().documentDirty);
  app.drafts->flush();
  EXPECT_FALSE(QFile::exists(directory_.filePath("active.aipdraft.json")));
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Проверяет настоящий гибридный запуск с синтетической моделью и сохранение из окна.
TEST_F(GuiAcceptance, SyntheticModelHybridSavePassesCli)
{
  Desktop app(directory_.path());
  const QString source = problem();
  app.open(source);
  const QDir fixture(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/tests/fixtures/models/polygon-policy-smoke"));
  const QString modelPath = directory_.filePath(QStringLiteral("модель 漢字"));
  ASSERT_TRUE(QDir().mkpath(modelPath));
  for (const QString & file : fixture.entryList(QDir::Files))
    ASSERT_TRUE(QFile::copy(fixture.filePath(file), modelPath + '/' + file));
  chooseFile(modelPath);
  action(app.window, "openModelAction");
  ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().modelReady; }));
  const auto modelId = app.workspace->snapshot().modelId;
  chooseFile(directory_.path());
  action(app.window, "openModelAction");
  ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().modelState == PolygonModelState::Error; }));
  EXPECT_TRUE(app.workspace->snapshot().modelReady);
  EXPECT_EQ(app.workspace->snapshot().modelId, modelId);
  EXPECT_FALSE(widget<QLabel>(app.window, "polygonModelMessage")->text().isEmpty());
  auto * tabs = widget<QTabWidget>(app.window, "workspaceRightTabs");
  click(tabs->tabBar(), tabs->tabBar()->tabRect(1).center());
  auto * mode = widget<QComboBox>(app.window, "polygonSolverBox");
  mode->setFocus();
  key(mode, Qt::Key_Home);
  for (int index = 0; index < 3; ++index)
    key(mode, Qt::Key_Down);
  key(&app.window, Qt::Key_Return, Qt::ControlModifier);
  ASSERT_TRUE(waitUntil([&]() { return app.workspace->snapshot().canSave; }, 30000));
  const QString solution = directory_.filePath("hybrid.json");
  chooseFile(solution);
  key(&app.window, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
  QProcess cli;
  cli.start(QStringLiteral(AIPACKAGING_CLI_PATH), {"validate", "--problem", source, "--solution", solution});
  ASSERT_TRUE(cli.waitForFinished(30000));
  EXPECT_EQ(cli.exitCode(), 0) << cli.readAllStandardError().toStdString();
}
#endif
} // namespace acceptance
