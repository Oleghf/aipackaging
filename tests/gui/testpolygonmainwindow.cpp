#include <memory>
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
  window.presentPolygonWorkspace(running);
  EXPECT_FALSE(window.findChild<QPushButton *>("polygonOpenButton")->isEnabled());
  EXPECT_FALSE(window.findChild<QPushButton *>("polygonStartButton")->isEnabled());
  EXPECT_TRUE(window.findChild<QPushButton *>("polygonCancelButton")->isEnabled());

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
}
