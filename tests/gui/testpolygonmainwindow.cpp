#include <memory>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <polygonmainwindow.h>
#include <polygonstartpage.h>
#include <polygonworkspacewidget.h>

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

/// Проверяет, что новое окно содержит только полигональную рабочую область.
TEST(PolygonMainWindow, HasOnlyPolygonWorkspace)
{
  application();
  QSettings().clear();
  PolygonMainWindow window;
  EXPECT_NE(window.findChild<PolygonWorkspaceWidget *>(), nullptr);
  EXPECT_EQ(window.findChild<QTabWidget *>(), nullptr);
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

/// Проверяет стартовую страницу, примеры и честно недоступные действия будущего M7.
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
  EXPECT_FALSE(create->isEnabled());
  EXPECT_FALSE(importDxf->isEnabled());
  EXPECT_EQ(examples->count(), 3);
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
}
