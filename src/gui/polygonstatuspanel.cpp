#include <algorithm>
#include <cstdint>
#include <QLabel>
#include <QProgressBar>
#include <QTextEdit>
#include <QVBoxLayout>

#include <polygonstatuspanel.h>

/// Создаёт элементы состояния с прежними размерами и именами объектов Qt.
PolygonStatusPanel::PolygonStatusPanel(QWidget * parent)
  : QWidget(parent)
  , statusLabel_(new QLabel(tr("Откройте polygon_problem v1"), this))
  , stageLabel_(new QLabel(this))
  , progressBar_(new QProgressBar(this))
  , metricsText_(new QTextEdit(this))
{
  statusLabel_->setObjectName(QStringLiteral("polygonStatus"));
  statusLabel_->setWordWrap(true);
  stageLabel_->setObjectName(QStringLiteral("polygonProgressStage"));
  progressBar_->setObjectName(QStringLiteral("polygonProgress"));
  metricsText_->setReadOnly(true);
  metricsText_->setMinimumHeight(165);
  metricsText_->setMaximumHeight(130);

  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(statusLabel_);
  layout->addWidget(stageLabel_);
  layout->addWidget(progressBar_);
  layout->addWidget(metricsText_);
}

/// Формирует пользовательское состояние и не подменяет фактическую долю частичного результата значением 100 процентов.
void PolygonStatusPanel::present(const PolygonWorkspaceSnapshot & snapshot)
{
  statusLabel_->setText(QString::fromStdString(snapshot.statusText));
  statusLabel_->setStyleSheet(snapshot.partial ? QStringLiteral("color:#B45309;font-weight:600") : QString());
  switch (snapshot.progress.stage)
  {
    case NestingProgressStage::Instances:
      stageLabel_->setText(tr("Этап: размещение деталей"));
      break;
    case NestingProgressStage::RandomIterations:
      stageLabel_->setText(tr("Этап: случайные попытки"));
      break;
    case NestingProgressStage::ExpandedStates:
      stageLabel_->setText(tr("Этап: лучевой поиск"));
      break;
    case NestingProgressStage::NeuralRollouts:
      stageLabel_->setText(tr("Этап: нейросетевые прогоны"));
      break;
  }

  if (snapshot.canCancel && snapshot.progress.total == 0)
  {
    progressBar_->setRange(0, 0);
  }
  else
  {
    progressBar_->setRange(0, 1000);
    const std::uint64_t total = snapshot.progress.total;
    const bool solved =
      snapshot.state == PolygonWorkspaceState::Completed && !snapshot.partial && snapshot.solutionStatus == "solved";
    const int value =
      solved ? 1000
             : (total == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(1000, snapshot.progress.completed * 1000 / total)));
    progressBar_->setValue(value);
  }

  const auto & objective = snapshot.objective;
  const auto & metrics = snapshot.metrics;
  metricsText_->setPlainText(
    tr("Решатель: %1\nСтатус решения: %2\nРазмещено: %3 / %4\nЗанятая длина: %5 мм\nПравая полоса: %6 мм\n"
       "Доп. прямоугольник: %7 мм²\nФрагментация: %8 мм²\nИспользование материала: %9 %\n"
       "Кандидаты: %10\nРаскрытые состояния: %11\nВремя: %12 мс")
      .arg(QString::fromStdString(snapshot.solverName.empty() ? "—" : snapshot.solverName))
      .arg(QString::fromStdString(snapshot.solutionStatus.empty() ? "—" : snapshot.solutionStatus))
      .arg(objective.placedParts)
      .arg(objective.totalParts)
      .arg(static_cast<double>(objective.usedLength) / 1000.0, 0, 'f', 3)
      .arg(static_cast<double>(objective.primaryRemnantWidth) / 1000.0, 0, 'f', 3)
      .arg(static_cast<double>(objective.largestExtraRectangleArea) / 1'000'000.0, 0, 'f', 3)
      .arg(static_cast<double>(objective.fragmentationPenalty) / 1'000'000.0, 0, 'f', 3)
      .arg(objective.materialUtilization * 100.0, 0, 'f', 2)
      .arg(metrics.candidatesGenerated)
      .arg(metrics.expandedStates)
      .arg(static_cast<double>(metrics.totalTimeUs) / 1000.0, 0, 'f', 3));
}
