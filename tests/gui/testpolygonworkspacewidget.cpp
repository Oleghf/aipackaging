#include <cstdint>
#include <limits>
#include <memory>
#include <QApplication>
#include <QColor>
#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QThread>
#include <QToolButton>
#include <stdexcept>
#include <utility>

#include <gtest/gtest.h>
#include <polygoncanvaswidget.h>
#include <polygonworkspacewidget.h>
#include <qtapplicationdispatcher.h>

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

/// Проверяет, что диспетчер не выполняет функцию синхронно и доставляет её в поток Qt.
TEST(PolygonWorkspaceWidget, DispatcherQueuesCallbackToQtThread)
{
  QApplication * application = ensureApplication();
  QtApplicationDispatcher dispatcher(application);
  bool called = false;
  QThread * deliveredThread = nullptr;
  dispatcher.post(
    [&]()
    {
      called = true;
      deliveredThread = QThread::currentThread();
    });
  EXPECT_FALSE(called);
  application->processEvents();
  EXPECT_TRUE(called);
  EXPECT_EQ(deliveredThread, application->thread());
}

/// Проверяет базовый алгоритм по умолчанию, сигналы и блокировку элементов управления.
TEST(PolygonWorkspaceWidget, ExposesConfiguredActionsAndRunningState)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  const auto config = widget.solverConfig();
  EXPECT_EQ(config.algorithm, BaselineAlgorithm::AreaLeftBottom);
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

/// Проверяет фактическую долю частичного результата без принудительных 100 процентов.
TEST(PolygonWorkspaceWidget, PreservesPartialProgressOnCompletion)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Completed;
  snapshot.partial = true;
  snapshot.solutionStatus = "budget_exhausted";
  snapshot.progress.completed = 1;
  snapshot.progress.total = 4;
  widget.present(snapshot);
  auto * progress = widget.findChild<QProgressBar *>("polygonProgress");
  ASSERT_NE(progress, nullptr);
  EXPECT_EQ(progress->value(), 250);
}

/// Проверяет полный диапазон начального значения и явный отказ от переполнения.
TEST(PolygonWorkspaceWidget, ValidatesSeedWithoutSilentFallback)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Ready;
  snapshot.canRun = true;
  widget.present(snapshot);
  auto * seed = widget.findChild<QLineEdit *>("polygonSeedEdit");
  auto * start = widget.findChild<QPushButton *>("polygonStartButton");
  auto * diagnostic = widget.findChild<QLabel *>("polygonSeedValidation");
  ASSERT_NE(seed, nullptr);
  ASSERT_NE(start, nullptr);
  ASSERT_NE(diagnostic, nullptr);

  seed->setText(QStringLiteral("0"));
  EXPECT_TRUE(widget.settingsValid());
  EXPECT_EQ(widget.solverConfig().seed, 0U);
  EXPECT_TRUE(start->isEnabled());
  seed->setText(QStringLiteral("18446744073709551615"));
  EXPECT_TRUE(widget.settingsValid());
  EXPECT_EQ(widget.solverConfig().seed, std::numeric_limits<std::uint64_t>::max());
  seed->setText(QStringLiteral("18446744073709551616"));
  EXPECT_FALSE(widget.settingsValid());
  EXPECT_FALSE(start->isEnabled());
  EXPECT_FALSE(diagnostic->isHidden());
  EXPECT_THROW(widget.solverConfig(), std::invalid_argument);
  seed->clear();
  EXPECT_FALSE(widget.settingsValid());
  EXPECT_FALSE(start->isEnabled());
}

/// Проверяет русские подписи базовых алгоритмов и экспертных параметров.
TEST(PolygonWorkspaceWidget, UsesRussianUserFacingLabels)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  auto * solvers = widget.findChild<QComboBox *>("polygonSolverBox");
  ASSERT_NE(solvers, nullptr);
  for (int index = 0; index < 5; ++index)
    EXPECT_TRUE(solvers->itemText(index).contains(QRegularExpression(QStringLiteral("[А-Яа-яЁё]"))));
}

/// Проверяет выбор комплекта модели и блокировку нейросетевого запуска без него.
TEST(PolygonWorkspaceWidget, RequiresVerifiedModelForNeuralMode)
{
  ensureApplication();
  PolygonWorkspaceWidget widget;
  bool modelRequested = false;
  QObject::connect(&widget, &PolygonWorkspaceWidget::requestOpenModel, [&modelRequested]() { modelRequested = true; });
  auto * modelButton = widget.findChild<QPushButton *>("polygonModelButton");
  auto * solver = widget.findChild<QComboBox *>("polygonSolverBox");
  auto * start = widget.findChild<QPushButton *>("polygonStartButton");
  ASSERT_NE(modelButton, nullptr);
  ASSERT_NE(solver, nullptr);
  ASSERT_NE(start, nullptr);
  modelButton->click();
  EXPECT_TRUE(modelRequested);

  PolygonWorkspaceSnapshot snapshot;
  snapshot.state = PolygonWorkspaceState::Ready;
  snapshot.canOpen = true;
  snapshot.canRun = true;
  snapshot.canLoadModel = true;
  widget.present(snapshot);
  solver->setCurrentIndex(5);
  EXPECT_EQ(widget.solverConfig().method, NestingMethod::Neural);
  EXPECT_FALSE(start->isEnabled());

  snapshot.modelReady = true;
  snapshot.modelId = "policy";
  snapshot.modelSha256 = "0123456789abcdef";
  widget.present(snapshot);
  EXPECT_TRUE(start->isEnabled());
  const auto * label = widget.findChild<QLabel *>("polygonModelStatus");
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains(QStringLiteral("0123456789ab")));
}
