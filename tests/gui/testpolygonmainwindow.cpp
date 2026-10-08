#include <limits>
#include <memory>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>

#include <gtest/gtest.h>
#include <polygondxfimportwizard.h>
#include <polygonmainwindow.h>
#include <polygonstartpage.h>
#include <polygonworkspacewidget.h>
#include <qtlocalization.h>

namespace
{
/// Возвращает один экземпляр приложения Qt для всех проверок окна.
QApplication * application()
{
  const auto configure = [](QApplication * app)
  {
    static QTemporaryDir settingsDirectory;
    QCoreApplication::setOrganizationName(QStringLiteral("AIPackagingTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AIPackagingGuiTests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    return app;
  };
  if (auto * existing = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return configure(existing);
  static int argumentCount = 1;
  static char name[] = "aipackaging-tests";
  static char * arguments[] = {name, nullptr};
  static auto created = std::make_unique<QApplication>(argumentCount, arguments);
  return configure(created.get());
}
} // namespace

/// Проверяет автономную справку и переход по локальной ссылке без запуска браузера или сети.
TEST(PolygonMainWindow, OpensOfflineHelp)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  auto * help = window.findChild<QAction *>("helpAction");
  ASSERT_NE(help, nullptr);
  EXPECT_EQ(help->shortcut(), QKeySequence::HelpContents);
  help->trigger();
  auto * browser = window.findChild<QTextBrowser *>("offlineHelpBrowser");
  ASSERT_NE(browser, nullptr);
  EXPECT_TRUE(browser->toPlainText().contains(QStringLiteral("руководство")));
  browser->anchorClicked(QUrl(QStringLiteral("quick-start.md")));
  EXPECT_TRUE(browser->toPlainText().contains(QStringLiteral("Первый раскрой")));
  const QUrl previous = browser->source();
  browser->anchorClicked(QUrl(QStringLiteral("https://example.invalid")));
  EXPECT_EQ(browser->source(), previous);
}

/// Сохраняет внешний выбор, относительное происхождение комплектной модели и пользовательский режим.
TEST(PolygonMainWindow, PreservesModelSourceAndUserPreset)
{
  application();
  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("polygon/modelDirectory"), QStringLiteral("C:/external-model"));
  settings.setValue(QStringLiteral("ui/runPreset"), 1);
  PolygonMainWindow window;
  QString requested;
  int count = 0;
  PolygonWorkspaceActions actions;
  actions.openModel = [&](const std::string & path)
  {
    requested = QString::fromStdString(path);
    ++count;
  };
  window.setPolygonWorkspaceActions(actions);
  EXPECT_EQ(requested, QStringLiteral("C:/external-model"));
  EXPECT_EQ(count, 1);
  PolygonWorkspaceSnapshot snapshot;
  snapshot.canLoadModel = true;
  snapshot.modelState = PolygonModelState::Error;
  window.presentPolygonWorkspace(snapshot);
  EXPECT_EQ(count, 1);
  auto * bundled = window.findChild<QAction *>("useBundledModelAction");
  ASSERT_NE(bundled, nullptr);
  bundled->trigger();
  EXPECT_EQ(count, 2);
  EXPECT_TRUE(requested.endsWith(QStringLiteral("/models/polygon-policy-v1")));
  snapshot.modelState = PolygonModelState::Ready;
  snapshot.modelReady = true;
  window.presentPolygonWorkspace(snapshot);
  EXPECT_EQ(settings.value(QStringLiteral("polygon/modelSource")).toString(), QStringLiteral("bundled"));
  EXPECT_FALSE(settings.contains(QStringLiteral("polygon/modelDirectory")));
  EXPECT_EQ(window.findChild<PolygonWorkspaceWidget *>()->selectedPreset(), 1);
}

/// Проверяет, что новое окно содержит только полигональную рабочую область.
TEST(PolygonMainWindow, InstallsRussianQtTranslation)
{
  application();
  ASSERT_TRUE(installRussianQtTranslation());
  QMessageBox box(QMessageBox::Question, QStringLiteral("Проверка"), QStringLiteral("Сохранить?"),
                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  EXPECT_TRUE(box.button(QMessageBox::Save)->text().contains(QStringLiteral("Сохранить")));
  EXPECT_TRUE(box.button(QMessageBox::Cancel)->text().contains(QStringLiteral("Отмена")));
  PolygonDxfImportWizard wizard;
  EXPECT_EQ(wizard.buttonText(QWizard::NextButton), QStringLiteral("Далее"));
}

/// Проверяет повторный запуск мастера для старой записи DXF без чтения её как задачи.
TEST(PolygonMainWindow, ReopensRecentDxfThroughImport)
{
  application();
  QSettings().clear();
  QTemporaryDir temporary;
  const QString path = temporary.filePath(QStringLiteral("part.DXF"));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.close();
  QSettings().setValue(QStringLiteral("files/recentProblems"), QStringList{path});
  PolygonMainWindow window;
  bool problemOpened = false;
  QString imported;
  PolygonWorkspaceActions actions;
  actions.openProblem = [&](const std::string &)
  {
    problemOpened = true;
  };
  window.setPolygonWorkspaceActions(actions);
  PolygonImportActions imports;
  imports.inspect = [&](const PolygonImportInspectionRequest & value)
  {
    imported = QString::fromStdString(value.filePath);
  };
  window.setPolygonImportActions(imports);
  window.findChild<PolygonStartPage *>()->requestOpenPath(path);
  EXPECT_EQ(imported, path);
  EXPECT_FALSE(problemOpened);
  ASSERT_NE(window.findChild<PolygonDxfImportWizard *>(), nullptr);
  static_cast<QDialog *>(window.findChild<PolygonDxfImportWizard *>())->reject();
  EXPECT_EQ(QSettings().value(QStringLiteral("files/recentSourceKinds")).toMap().value(path).toString(), QStringLiteral("dxf"));
}

/// Проверяет отдельную команду строгого сохранения исправленного черновика и отмену выбора пути.
TEST(PolygonMainWindow, SavesValidDraftAsProblem)
{
  application();
  QSettings().clear();
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
  QTemporaryDir temporary;
  PolygonMainWindow window;
  std::string saved;
  bool draft = true;
  PolygonWorkspaceActions actions;
  actions.saveDocument = [&](const std::string & path, bool asDraft)
  {
    saved = path;
    draft = asDraft;
  };
  window.setPolygonWorkspaceActions(actions);
  PolygonWorkspaceSnapshot snapshot;
  snapshot.hasDocument = true;
  snapshot.documentSource = PolygonDocumentSource::Draft;
  snapshot.documentValid = true;
  snapshot.canSaveDocument = true;
  window.presentPolygonWorkspace(snapshot);
  auto * save = window.findChild<QAction *>("saveProblemAsAction");
  ASSERT_NE(save, nullptr);
  ASSERT_TRUE(save->isEnabled());
  const QString path = temporary.filePath(QStringLiteral("task.json"));
  QTimer::singleShot(0,
                     [&]()
                     {
                       auto * dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                       ASSERT_NE(dialog, nullptr);
                       dialog->selectFile(path);
                       QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                     });
  save->trigger();
  EXPECT_EQ(QString::fromStdString(saved), path);
  EXPECT_FALSE(draft);
  saved.clear();
  QTimer::singleShot(0,
                     []()
                     {
                       if (auto * dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                         dialog->reject();
                     });
  save->trigger();
  EXPECT_TRUE(saved.empty());
  snapshot.documentValid = false;
  window.presentPolygonWorkspace(snapshot);
  EXPECT_FALSE(save->isEnabled());
}

/// Проверяет, что новое окно содержит только полигональную рабочую область.
TEST(PolygonMainWindow, HasOnlyPolygonWorkspace)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  EXPECT_NE(window.findChild<PolygonWorkspaceWidget *>(), nullptr);
  EXPECT_NE(window.findChild<QTabWidget *>(QStringLiteral("workspaceRightTabs")), nullptr);
  EXPECT_NE(window.findChild<PolygonStartPage *>(), nullptr);
  EXPECT_EQ(window.findChild<QStackedWidget *>("mainPages"), window.centralWidget());
  EXPECT_EQ(window.objectName(), QStringLiteral("polygonMainWindow"));
  EXPECT_NE(window.menuBar(), nullptr);
}

/// Проверяет, что снимки прикладного слоя управляют действиями нового окна.
TEST(PolygonMainWindow, PresentsRunningAndCompletedState)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  PolygonWorkspaceSnapshot running;
  running.state = PolygonWorkspaceState::Running;
  running.canOpen = false;
  running.canCancel = true;
  running.canLoadModel = false;
  window.presentPolygonWorkspace(running);
  EXPECT_FALSE(window.findChild<QPushButton *>("polygonOpenButton")->isEnabled());
  EXPECT_FALSE(window.findChild<QPushButton *>("polygonStartButton")->isEnabled());
  EXPECT_TRUE(window.findChild<QPushButton *>("polygonCancelButton")->isEnabled());
  auto * openAction = window.findChild<QAction *>("openProblemAction");
  auto * saveAction = window.findChild<QAction *>("saveSolutionAction");
  auto * modelAction = window.findChild<QAction *>("openModelAction");
  ASSERT_NE(openAction, nullptr);
  ASSERT_NE(saveAction, nullptr);
  ASSERT_NE(modelAction, nullptr);
  EXPECT_FALSE(openAction->isEnabled());
  EXPECT_FALSE(saveAction->isEnabled());
  EXPECT_FALSE(modelAction->isEnabled());

  PolygonWorkspaceSnapshot complete;
  complete.state = PolygonWorkspaceState::Completed;
  complete.canOpen = true;
  complete.canRun = true;
  complete.canSave = true;
  complete.partial = true;
  complete.statusText = "Частичное решение";
  window.presentPolygonWorkspace(complete);
  EXPECT_TRUE(window.findChild<QPushButton *>("polygonOpenButton")->isEnabled());
  EXPECT_TRUE(window.findChild<QPushButton *>("polygonSaveButton")->isEnabled());
  EXPECT_FALSE(window.findChild<QPushButton *>("polygonCancelButton")->isEnabled());
  EXPECT_TRUE(openAction->isEnabled());
  EXPECT_TRUE(saveAction->isEnabled());
  EXPECT_TRUE(modelAction->isEnabled());
}

/// Проверяет стартовую страницу, примеры и доступные создание документа и импорт.
TEST(PolygonMainWindow, ShowsFriendlyStartPage)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  auto * start = window.findChild<PolygonStartPage *>();
  auto * create = window.findChild<QPushButton *>("startCreateButton");
  auto * importDxf = window.findChild<QPushButton *>("startImportButton");
  auto * examples = window.findChild<QListWidget *>("exampleProblemList");
  ASSERT_NE(start, nullptr);
  ASSERT_NE(create, nullptr);
  ASSERT_NE(importDxf, nullptr);
  ASSERT_NE(examples, nullptr);
  EXPECT_TRUE(create->isEnabled());
  EXPECT_FALSE(importDxf->isEnabled());
  EXPECT_EQ(examples->count(), 3);
}

/// Проверяет создание задачи через обязательные поля и пересылку команд истории.
TEST(PolygonMainWindow, CreatesDocumentAndForwardsHistoryActions)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  std::string problemId;
  double width = 0.0;
  double height = 0.0;
  int undoCount = 0;
  int redoCount = 0;
  PolygonWorkspaceActions actions;
  actions.createDocument = [&](const std::string & id, double valueWidth, double valueHeight)
  {
    problemId = id;
    width = valueWidth;
    height = valueHeight;
  };
  actions.undoDocument = [&]()
  {
    ++undoCount;
  };
  actions.redoDocument = [&]()
  {
    ++redoCount;
  };
  window.setPolygonWorkspaceActions(std::move(actions));
  PolygonWorkspaceSnapshot snapshot;
  snapshot.canOpen = true;
  snapshot.canUndo = true;
  snapshot.canRedo = true;
  snapshot.undoLabel = "Свойства листа";
  snapshot.redoLabel = "Добавление детали";
  window.presentPolygonWorkspace(snapshot);

  QTimer::singleShot(
    0,
    []()
    {
      auto * dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      ASSERT_NE(dialog, nullptr);
      const auto * buttons = dialog->findChild<QDialogButtonBox *>();
      ASSERT_NE(buttons, nullptr);
      EXPECT_EQ(buttons->button(QDialogButtonBox::Ok)->text(), QStringLiteral("Создать"));
      EXPECT_EQ(buttons->button(QDialogButtonBox::Cancel)->text(), QStringLiteral("Отменить"));
      dialog->findChild<QLineEdit *>(QStringLiteral("newProblemId"))->setText(QStringLiteral("созданная-задача"));
      dialog->findChild<QDoubleSpinBox *>(QStringLiteral("newSheetWidth"))->setValue(250.0);
      dialog->findChild<QDoubleSpinBox *>(QStringLiteral("newSheetHeight"))->setValue(125.0);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
  window.findChild<QAction *>(QStringLiteral("createDocumentAction"))->trigger();
  EXPECT_EQ(problemId, "созданная-задача");
  EXPECT_DOUBLE_EQ(width, 250.0);
  EXPECT_DOUBLE_EQ(height, 125.0);

  auto * undo = window.findChild<QAction *>(QStringLiteral("undoDocumentAction"));
  auto * redo = window.findChild<QAction *>(QStringLiteral("redoDocumentAction"));
  ASSERT_NE(undo, nullptr);
  ASSERT_NE(redo, nullptr);
  EXPECT_TRUE(undo->text().contains(QStringLiteral("Свойства листа")));
  EXPECT_TRUE(redo->text().contains(QStringLiteral("Добавление детали")));
  undo->trigger();
  redo->trigger();
  EXPECT_EQ(undoCount, 1);
  EXPECT_EQ(redoCount, 1);
}

/// Проверяет, что импорт становится доступен только после подключения отдельного сценария.
TEST(PolygonMainWindow, EnablesDxfImportOnlyWhenWorkspaceCanOpen)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  PolygonImportActions importActions;
  importActions.inspect = [](const PolygonImportInspectionRequest &) {};
  window.setPolygonImportActions(std::move(importActions));
  PolygonWorkspaceSnapshot ready;
  ready.canOpen = true;
  window.presentPolygonWorkspace(ready);
  auto * button = window.findChild<QPushButton *>("startImportButton");
  auto * action = window.findChild<QAction *>("importDxfAction");
  ASSERT_NE(button, nullptr);
  ASSERT_NE(action, nullptr);
  EXPECT_TRUE(button->isEnabled());
  EXPECT_TRUE(action->isEnabled());
  ready.canOpen = false;
  window.presentPolygonWorkspace(ready);
  EXPECT_FALSE(button->isEnabled());
  EXPECT_FALSE(action->isEnabled());
}

/// Проверяет немодальную карточку читаемого и повреждённого автоматического черновика.
TEST(PolygonMainWindow, PresentsRecoveryCardAndForwardsActions)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  int restored = 0;
  int removed = 0;
  PolygonWorkspaceActions actions;
  actions.restoreRecovery = [&restored]()
  {
    ++restored;
  };
  actions.deleteRecovery = [&removed]()
  {
    ++removed;
  };
  window.setPolygonWorkspaceActions(std::move(actions));

  PolygonWorkspaceSnapshot snapshot;
  snapshot.recovery = {true, true, true, "autosave.aipdraft.json", "problem.json", "2026-09-26T12:00:00Z", {}};
  window.presentPolygonWorkspace(snapshot);
  auto * card = window.findChild<QWidget *>("polygonRecoveryCard");
  auto * restore = window.findChild<QPushButton *>("restoreRecoveryButton");
  auto * remove = window.findChild<QPushButton *>("deleteRecoveryButton");
  ASSERT_NE(card, nullptr);
  ASSERT_NE(restore, nullptr);
  ASSERT_NE(remove, nullptr);
  EXPECT_FALSE(card->isHidden());
  EXPECT_TRUE(restore->isEnabled());
  restore->click();
  remove->click();
  EXPECT_EQ(restored, 1);
  EXPECT_EQ(removed, 1);

  snapshot.recovery.restorable = false;
  snapshot.recovery.error = "повреждён";
  window.presentPolygonWorkspace(snapshot);
  EXPECT_FALSE(restore->isEnabled());
}

/// Проверяет разделение сочетаний сохранения документа и решения и задержку автосохранения.
TEST(PolygonMainWindow, UsesSeparateSaveShortcutsAndAutosaveDelay)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  auto * document = window.findChild<QAction *>("saveDocumentAction");
  auto * solution = window.findChild<QAction *>("saveSolutionAction");
  auto * timer = window.findChild<QTimer *>("polygonAutosaveTimer");
  ASSERT_NE(document, nullptr);
  ASSERT_NE(solution, nullptr);
  ASSERT_NE(timer, nullptr);
  EXPECT_EQ(document->shortcut(), QKeySequence::Save);
  EXPECT_EQ(solution->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  EXPECT_TRUE(timer->isSingleShot());
  EXPECT_EQ(timer->interval(), 2000);
}

/// Проверяет восстановление пользовательского режима и ограниченного списка недавних файлов.
TEST(PolygonMainWindow, RestoresWorkspaceSettingsAndMarksMissingRecentFiles)
{
  application();
  QSettings settings;
  settings.clear();
  QStringList recent;
  for (int index = 0; index < 9; ++index)
    recent.push_back(QStringLiteral("C:/aipackaging-missing-%1.json").arg(index));
  settings.setValue(QStringLiteral("files/recentProblems"), recent);
  settings.setValue(QStringLiteral("ui/expertExpanded"), true);
  settings.setValue(QStringLiteral("ui/runPreset"), 1);
  settings.sync();

  PolygonMainWindow window;
  auto * workspace = window.findChild<PolygonWorkspaceWidget *>();
  auto * recentList = window.findChild<QListWidget *>("recentProblemList");
  ASSERT_NE(workspace, nullptr);
  ASSERT_NE(recentList, nullptr);
  EXPECT_TRUE(workspace->advancedExpanded());
  EXPECT_EQ(workspace->selectedPreset(), 1);
  ASSERT_EQ(recentList->count(), 8);
  EXPECT_TRUE(recentList->item(0)->text().contains(QStringLiteral("файл недоступен")));
}

/// Проверяет относительный комплект при повторном запуске и сохранение выбора во время проверки модели.
TEST(PolygonMainWindow, RetriesBundledModelWithoutRememberingAbsolutePath)
{
  application();
  QSettings().clear();
  QSettings().setValue(QStringLiteral("polygon/modelSource"), QStringLiteral("bundled"));
  PolygonMainWindow window;
  std::string requested;
  PolygonWorkspaceActions actions;
  actions.openModel = [&](const std::string & path)
  {
    requested = path;
  };
  window.setPolygonWorkspaceActions(actions);
  EXPECT_TRUE(QString::fromStdString(requested).endsWith(QStringLiteral("/models/polygon-policy-v1")));
  EXPECT_FALSE(QSettings().contains(QStringLiteral("polygon/modelDirectory")));
  auto * workspace = window.findChild<PolygonWorkspaceWidget *>();
  ASSERT_NE(workspace, nullptr);
  workspace->selectPreset(1);
  PolygonWorkspaceSnapshot snapshot;
  snapshot.modelState = PolygonModelState::Ready;
  snapshot.modelReady = true;
  window.presentPolygonWorkspace(snapshot);
  EXPECT_EQ(workspace->selectedPreset(), 1);
  EXPECT_FALSE(QSettings().contains(QStringLiteral("polygon/modelDirectory")));
}

/// Проверяет ограничение повреждённых размеров, положения панелей и режима запуска безопасными значениями.
TEST(PolygonMainWindow, RejectsCorruptedWindowSettings)
{
  application();
  QSettings settings;
  settings.clear();
  settings.setValue(QStringLiteral("ui/mainWindowGeometry"), QByteArray("повреждено"));
  settings.setValue(QStringLiteral("ui/mainWindowState"), QByteArray("повреждено"));
  settings.setValue(QStringLiteral("ui/workspaceSplitter"), QByteArray("повреждено"));
  settings.setValue(QStringLiteral("ui/runPreset"), std::numeric_limits<int>::max());
  settings.sync();

  PolygonMainWindow window;
  auto * workspace = window.findChild<PolygonWorkspaceWidget *>();
  ASSERT_NE(workspace, nullptr);
  EXPECT_GE(window.width(), 1280);
  EXPECT_GE(window.height(), 720);
  EXPECT_EQ(workspace->selectedPreset(), 3);
}

/// Проверяет доступность основных элементов при минимальном рабочем размере и системном масштабе.
TEST(PolygonMainWindow, FitsMinimumResolutionAndExposesAccessibleControls)
{
  QApplication * app = application();
  QSettings().clear();
  PolygonMainWindow window;
  window.resize(1280, 720);
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Ready;
  snapshot.problemId = "long-production-problem-name-for-accessibility";
  snapshot.canOpen = true;
  snapshot.canRun = true;
  snapshot.document.sourceIdentifier = "problem.json";
  snapshot.documentValid = true;
  snapshot.hasDocument = true;
  snapshot.canEdit = true;
  snapshot.editableDocument = std::make_shared<const aipackaging::editor::EditablePolygonDocument>();
  window.presentPolygonWorkspace(snapshot);
  window.show();
  app->processEvents();

  auto * modes = window.findChild<QComboBox *>("polygonSolverBox");
  auto * canvas = window.findChild<QWidget *>("polygonCanvas");
  auto * start = window.findChild<QPushButton *>("polygonStartButton");
  ASSERT_NE(modes, nullptr);
  ASSERT_NE(canvas, nullptr);
  ASSERT_NE(start, nullptr);
  EXPECT_TRUE(modes->isVisible());
  EXPECT_TRUE(canvas->isVisible());
  EXPECT_TRUE(start->isVisible());
  EXPECT_FALSE(modes->accessibleName().isEmpty());
  EXPECT_GE(window.width(), 1280);
  EXPECT_GE(window.height(), 720);
  auto * tabs = window.findChild<QTabWidget *>("workspaceRightTabs");
  auto * metrics = window.findChild<QToolButton *>("polygonMetricsToggle");
  ASSERT_NE(tabs, nullptr);
  ASSERT_NE(metrics, nullptr);
  EXPECT_FALSE(metrics->isChecked());
  tabs->setCurrentIndex(0);
  app->processEvents();
  auto * add = window.findChild<QPushButton *>("addPartButton");
  ASSERT_NE(add, nullptr);
  QScrollArea * forms = nullptr;
  for (QWidget * parent = add->parentWidget(); parent; parent = parent->parentWidget())
    if ((forms = qobject_cast<QScrollArea *>(parent)))
      break;
  ASSERT_NE(forms, nullptr);
  forms->ensureWidgetVisible(add);
  app->processEvents();
  EXPECT_GE(forms->viewport()->height(), 175);
  EXPECT_TRUE(forms->viewport()->rect().contains(add->mapTo(forms->viewport(), add->rect().center())));
  tabs->setCurrentIndex(1);
  auto * advanced = window.findChild<QToolButton *>("polygonAdvancedToggle");
  ASSERT_NE(advanced, nullptr);
  advanced->setChecked(true);
  auto * runScroll = window.findChild<QScrollArea *>("polygonRunScroll");
  ASSERT_NE(runScroll, nullptr);
  runScroll->ensureWidgetVisible(start);
  app->processEvents();
  EXPECT_TRUE(runScroll->viewport()->rect().contains(start->mapTo(runScroll->viewport(), start->rect().center())));
  EXPECT_EQ(window.size(), QSize(1280, 720));
}
