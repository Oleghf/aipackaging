#include <memory>
#include <QAction>
#include <QApplication>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>

#include <gtest/gtest.h>
#include <polygonmainwindow.h>
#include <polygonworkspacewidget.h>

namespace
{
/// Возвращает один экземпляр приложения Qt для всех проверок окна.
QApplication * application()
{
  if (auto * existing = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return existing;
  static int argumentCount = 1;
  static char name[] = "aipackaging-tests";
  static char * arguments[] = {name, nullptr};
  static auto created = std::make_unique<QApplication>(argumentCount, arguments);
  return created.get();
}
} // namespace

/// Проверяет, что новое окно содержит только полигональную рабочую область.
TEST(PolygonMainWindow, HasOnlyPolygonWorkspace)
{
  application();
  PolygonMainWindow window;
  EXPECT_NE(window.findChild<PolygonWorkspaceWidget *>(), nullptr);
  EXPECT_EQ(window.findChild<QTabWidget *>(), nullptr);
  EXPECT_EQ(window.findChild<QWidget *>("polygonWorkspace"), window.centralWidget());
  EXPECT_EQ(window.objectName(), QStringLiteral("polygonMainWindow"));
  EXPECT_NE(window.menuBar(), nullptr);
}

/// Проверяет, что снимки прикладного слоя управляют действиями нового окна.
TEST(PolygonMainWindow, PresentsRunningAndCompletedState)
{
  application();
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
  ASSERT_EQ(window.menuBar()->actions().size(), 1);
  const QList<QAction *> fileActions = window.menuBar()->actions().front()->menu()->actions();
  ASSERT_EQ(fileActions.size(), 3);
  EXPECT_FALSE(fileActions[0]->isEnabled());
  EXPECT_FALSE(fileActions[1]->isEnabled());
  EXPECT_FALSE(fileActions[2]->isEnabled());

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
  EXPECT_TRUE(fileActions[0]->isEnabled());
  EXPECT_TRUE(fileActions[1]->isEnabled());
  EXPECT_TRUE(fileActions[2]->isEnabled());
}
