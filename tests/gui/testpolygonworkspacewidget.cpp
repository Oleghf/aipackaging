#include <memory>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
#include <utility>

#include <gtest/gtest.h>
#include <polygoncanvaswidget.h>
#include <polygonworkspacewidget.h>
#include <qtview.h>

namespace
{
/// Создаёт единственный `QApplication` для тестов виджетов либо возвращает существующий.
QApplication * ensureApplication()
{
  if (auto * application = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return application;
  static int argumentCount = 1;
  static char applicationName[] = "aipackaging-tests";
  static char * arguments[] = {applicationName, nullptr};
  static auto application = std::make_unique<QApplication>(argumentCount, arguments);
  return application.get();
}
} // namespace

/// Проверяет сохранение клеточного экрана и добавление отдельной полигональной вкладки.
TEST(PolygonWorkspaceWidget, MainWindowContainsTwoWorkspaces)
{
  ensureApplication();
  QtView view;
  const auto * tabs = view.findChild<QTabWidget *>("workspaceTabs");
  ASSERT_NE(tabs, nullptr);
  EXPECT_EQ(tabs->count(), 2);
  EXPECT_EQ(tabs->tabText(0), QStringLiteral("Клеточный прототип"));
  EXPECT_EQ(tabs->tabText(1), QStringLiteral("Полигональный раскрой"));
}

/// Проверяет базовый алгоритм по умолчанию, сигналы и блокировку элементов управления.
TEST(PolygonWorkspaceWidget, ExposesConfiguredActionsAndRunningState)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  const auto config = widget.solverConfig();
  EXPECT_EQ(config.solver, aipackaging::solver::SolverKind::AreaLeftBottom);
  EXPECT_EQ(config.seed, 42);
  EXPECT_EQ(config.randomIterations, 64);
  EXPECT_EQ(config.beamWidth, 32);
  EXPECT_EQ(config.maxExpandedStates, 50000);
  EXPECT_EQ(config.timeoutMs, 30000);

  bool startRequested = false;
  bool cancelRequested = false;
  bool openRequested = false;
  bool saveRequested = false;
  QObject::connect(&widget, &PolygonWorkspaceWidget::requestStart, [&startRequested]() { startRequested = true; });
  QObject::connect(&widget, &PolygonWorkspaceWidget::requestCancel, [&cancelRequested]() { cancelRequested = true; });
  QObject::connect(&widget, &PolygonWorkspaceWidget::requestOpenProblem, [&openRequested]() { openRequested = true; });
  QObject::connect(&widget, &PolygonWorkspaceWidget::requestSaveSolution, [&saveRequested]() { saveRequested = true; });

  PolygonWorkspaceSnapshot ready;
  ready.state = PolygonWorkspaceState::Ready;
  ready.canOpen = true;
  ready.canRun = true;
  ready.canSave = true;
  widget.present(ready);
  auto * advancedToggle = widget.findChild<QToolButton *>("polygonAdvancedToggle");
  auto * advancedGroup = widget.findChild<QWidget *>("polygonAdvancedGroup");
  ASSERT_NE(advancedToggle, nullptr);
  ASSERT_NE(advancedGroup, nullptr);
  EXPECT_TRUE(advancedGroup->isHidden());
  advancedToggle->click();
  EXPECT_FALSE(advancedGroup->isHidden());
  auto * open = widget.findChild<QPushButton *>("polygonOpenButton");
  auto * save = widget.findChild<QPushButton *>("polygonSaveButton");
  ASSERT_NE(open, nullptr);
  ASSERT_NE(save, nullptr);
  open->click();
  save->click();
  EXPECT_TRUE(openRequested);
  EXPECT_TRUE(saveRequested);

  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Running;
  snapshot.canCancel = true;
  snapshot.statusText = "Выполняется";
  widget.present(snapshot);
  auto * start = widget.findChild<QPushButton *>("polygonStartButton");
  auto * cancel = widget.findChild<QPushButton *>("polygonCancelButton");
  ASSERT_NE(start, nullptr);
  ASSERT_NE(cancel, nullptr);
  EXPECT_FALSE(start->isEnabled());
  EXPECT_TRUE(cancel->isEnabled());
  start->click();
  cancel->click();
  EXPECT_FALSE(startRequested);
  EXPECT_TRUE(cancelRequested);
}

/// Проверяет отрисовку внешнего кольца и прозрачного отверстия без изменения модели.
TEST(PolygonWorkspaceWidget, RendersPolygonWithHole)
{
  ensureApplication();
  PolygonCanvasWidget canvas;
  canvas.resize(640, 480);
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Completed;
  snapshot.scene.sheetWidth = 100.0;
  snapshot.scene.sheetHeight = 70.0;
  snapshot.scene.sheetMargin = 2.0;
  snapshot.scene.usedLength = 40.0;
  PolygonPlacedPartView part;
  part.outer = {{5.0, 5.0}, {45.0, 5.0}, {45.0, 45.0}, {5.0, 45.0}};
  part.holes.push_back({{15.0, 15.0}, {35.0, 15.0}, {35.0, 35.0}, {15.0, 35.0}});
  snapshot.scene.placements.push_back(std::move(part));
  canvas.setSnapshot(snapshot);
  QImage image(canvas.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  canvas.render(&image);
  EXPECT_FALSE(image.isNull());
  EXPECT_NE(image.pixelColor(image.width() / 2, image.height() / 2), QColor(Qt::transparent));
}

/// Проверяет предупреждение и перечень экземпляров для частичной раскладки.
TEST(PolygonWorkspaceWidget, HighlightsPartialAndListsUnplacedInstances)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Completed;
  snapshot.partial = true;
  snapshot.statusText = "Показано лучшее частичное решение";
  snapshot.unplacedInstances = {"bracket #1", "panel #0"};
  widget.present(snapshot);

  auto * status = widget.findChild<QLabel *>("polygonStatus");
  auto * unplaced = widget.findChild<QListWidget *>();
  ASSERT_NE(status, nullptr);
  ASSERT_NE(unplaced, nullptr);
  EXPECT_TRUE(status->styleSheet().contains(QStringLiteral("font-weight")));
  EXPECT_EQ(unplaced->count(), 2);
  EXPECT_EQ(unplaced->item(0)->text(), QStringLiteral("bracket #1"));
}
