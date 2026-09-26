#include <cstdint>
#include <limits>
#include <memory>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>

#include <gtest/gtest.h>
#include <polygondocumentpanel.h>
#include <polygonrunpanel.h>
#include <polygonstatuspanel.h>

namespace
{
/// Создаёт единственное приложение Qt либо возвращает экземпляр из другого тестового файла.
QApplication * ensurePanelApplication()
{
  if (auto * application = qobject_cast<QApplication *>(QCoreApplication::instance()))
    return application;
  static int argumentCount = 1;
  static char applicationName[] = "aipackaging-panel-tests";
  static char * arguments[] = {applicationName, nullptr};
  static auto application = std::make_unique<QApplication>(argumentCount, arguments);
  return application.get();
}
} // namespace

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
  auto * unplaced = panel.findChild<QListWidget *>();
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
