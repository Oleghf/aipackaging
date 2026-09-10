#include <algorithm>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QSplitter>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include <polygoncanvaswidget.h>
#include <polygonworkspacewidget.h>

namespace
{
using namespace aipackaging::solver;

/// Преобразует внутренний статус baseline в подпись поля выбора.
QString solverTitle(SolverKind kind)
{
  switch (kind)
  {
    case SolverKind::InputFirstFit:
      return QStringLiteral("Input first-fit");
    case SolverKind::AreaLeftBottom:
      return QStringLiteral("Area left-bottom");
    case SolverKind::MaxSideLeftBottom:
      return QStringLiteral("Max-side left-bottom");
    case SolverKind::RandomLeftBottom:
      return QStringLiteral("Random left-bottom");
    case SolverKind::Beam:
      return QStringLiteral("Beam search");
  }
  return {};
}

/// Добавляет вариант solver с wire-именем в user data.
void addSolver(QComboBox * box, SolverKind kind)
{
  box->addItem(solverTitle(kind), QString::fromStdString(toString(kind)));
}
} // namespace

/// Собирает самостоятельную полигональную вкладку, не меняющую клеточный widget.
PolygonWorkspaceWidget::PolygonWorkspaceWidget(QWidget * parent)
  : QWidget(parent)
  , canvas_(new PolygonCanvasWidget(this))
  , openButton_(new QPushButton(tr("Открыть задачу"), this))
  , saveButton_(new QPushButton(tr("Сохранить решение"), this))
  , startButton_(new QPushButton(tr("Запустить"), this))
  , cancelButton_(new QPushButton(tr("Отменить"), this))
  , fitButton_(new QPushButton(tr("Вписать лист"), this))
  , solverBox_(new QComboBox(this))
  , advancedToggle_(new QToolButton(this))
  , advancedGroup_(new QGroupBox(this))
  , seedEdit_(new QLineEdit(QStringLiteral("42"), advancedGroup_))
  , randomIterationsSpin_(new QSpinBox(advancedGroup_))
  , beamWidthSpin_(new QSpinBox(advancedGroup_))
  , maxExpandedSpin_(new QSpinBox(advancedGroup_))
  , timeoutSpin_(new QSpinBox(advancedGroup_))
  , problemLabel_(new QLabel(tr("Задача: —"), this))
  , statusLabel_(new QLabel(tr("Откройте polygon_problem v1"), this))
  , progressBar_(new QProgressBar(this))
  , metricsText_(new QTextEdit(this))
  , unplacedList_(new QListWidget(this))
{
  setObjectName("polygonWorkspace");
  canvas_->setObjectName("polygonCanvas");
  openButton_->setObjectName("polygonOpenButton");
  saveButton_->setObjectName("polygonSaveButton");
  startButton_->setObjectName("polygonStartButton");
  cancelButton_->setObjectName("polygonCancelButton");
  solverBox_->setObjectName("polygonSolverBox");
  advancedToggle_->setObjectName("polygonAdvancedToggle");
  advancedGroup_->setObjectName("polygonAdvancedGroup");
  progressBar_->setObjectName("polygonProgress");
  statusLabel_->setObjectName("polygonStatus");

  addSolver(solverBox_, SolverKind::InputFirstFit);
  addSolver(solverBox_, SolverKind::AreaLeftBottom);
  addSolver(solverBox_, SolverKind::MaxSideLeftBottom);
  addSolver(solverBox_, SolverKind::RandomLeftBottom);
  addSolver(solverBox_, SolverKind::Beam);
  solverBox_->setCurrentIndex(1);

  seedEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{1,20}")), seedEdit_));
  for (QSpinBox * spin : {randomIterationsSpin_, beamWidthSpin_, maxExpandedSpin_})
    spin->setRange(1, 1'000'000);
  timeoutSpin_->setRange(0, 3'600'000);
  randomIterationsSpin_->setValue(64);
  beamWidthSpin_->setValue(32);
  maxExpandedSpin_->setValue(50'000);
  timeoutSpin_->setValue(30'000);

  QFormLayout * advancedLayout = new QFormLayout(advancedGroup_);
  advancedLayout->addRow(tr("Seed"), seedEdit_);
  advancedLayout->addRow(tr("Random iterations"), randomIterationsSpin_);
  advancedLayout->addRow(tr("Beam width"), beamWidthSpin_);
  advancedLayout->addRow(tr("Expanded states"), maxExpandedSpin_);
  advancedLayout->addRow(tr("Timeout, мс"), timeoutSpin_);
  advancedToggle_->setText(tr("Расширенные настройки"));
  advancedToggle_->setCheckable(true);
  advancedToggle_->setChecked(false);
  advancedToggle_->setArrowType(Qt::RightArrow);
  advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advancedGroup_->setVisible(false);

  metricsText_->setReadOnly(true);
  metricsText_->setMinimumHeight(165);
  unplacedList_->setMinimumHeight(100);
  statusLabel_->setWordWrap(true);

  QHBoxLayout * fileLayout = new QHBoxLayout();
  fileLayout->addWidget(openButton_);
  fileLayout->addWidget(saveButton_);
  fileLayout->addWidget(fitButton_);

  QHBoxLayout * runLayout = new QHBoxLayout();
  runLayout->addWidget(solverBox_, 1);
  runLayout->addWidget(startButton_);
  runLayout->addWidget(cancelButton_);

  QWidget * side = new QWidget(this);
  side->setMinimumWidth(310);
  side->setMaximumWidth(380);
  QVBoxLayout * sideLayout = new QVBoxLayout(side);
  sideLayout->addWidget(problemLabel_);
  sideLayout->addWidget(statusLabel_);
  sideLayout->addWidget(progressBar_);
  sideLayout->addLayout(runLayout);
  sideLayout->addWidget(advancedToggle_);
  sideLayout->addWidget(advancedGroup_);
  sideLayout->addWidget(new QLabel(tr("Метрики"), side));
  sideLayout->addWidget(metricsText_);
  sideLayout->addWidget(new QLabel(tr("Не размещены"), side));
  sideLayout->addWidget(unplacedList_, 1);

  QSplitter * splitter = new QSplitter(Qt::Horizontal, this);
  splitter->addWidget(canvas_);
  splitter->addWidget(side);
  splitter->setStretchFactor(0, 1);

  QVBoxLayout * root = new QVBoxLayout(this);
  root->setContentsMargins(18, 18, 18, 18);
  root->addLayout(fileLayout);
  root->addWidget(splitter, 1);

  connect(openButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestOpenProblem);
  connect(saveButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestSaveSolution);
  connect(startButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestStart);
  connect(cancelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestCancel);
  connect(fitButton_, &QPushButton::clicked, canvas_, &PolygonCanvasWidget::fitToView);
  connect(advancedToggle_, &QToolButton::toggled, this,
          [this](bool expanded)
          {
            advancedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            advancedGroup_->setVisible(expanded);
          });

  present({});
}

/// Парсит wire-имя solver и ограниченные виджетами значения конфигурации.
SolverConfig PolygonWorkspaceWidget::solverConfig() const
{
  SolverConfig result;
  parseSolverKind(solverBox_->currentData().toString().toStdString(), result.solver);
  bool seedValid = false;
  result.seed = seedEdit_->text().toULongLong(&seedValid);
  if (!seedValid)
    result.seed = 42;
  result.randomIterations = static_cast<std::size_t>(randomIterationsSpin_->value());
  result.beamWidth = static_cast<std::size_t>(beamWidthSpin_->value());
  result.maxExpandedStates = static_cast<std::size_t>(maxExpandedSpin_->value());
  result.timeoutMs = static_cast<std::uint64_t>(timeoutSpin_->value());
  return result;
}

/// Обновляет все поля из одного snapshot, исключая противоречивые состояния кнопок.
void PolygonWorkspaceWidget::present(const PolygonWorkspaceSnapshot & snapshot)
{
  problemLabel_->setText(snapshot.problemId.empty() ? tr("Задача: —")
                                                    : tr("Задача: %1").arg(QString::fromStdString(snapshot.problemId)));
  statusLabel_->setText(QString::fromStdString(snapshot.statusText));
  statusLabel_->setStyleSheet(snapshot.partial ? QStringLiteral("color:#B45309;font-weight:600") : QString());
  openButton_->setEnabled(snapshot.canOpen);
  saveButton_->setEnabled(snapshot.canSave);
  startButton_->setEnabled(snapshot.canRun);
  cancelButton_->setEnabled(snapshot.canCancel);
  solverBox_->setEnabled(snapshot.canRun);
  advancedToggle_->setEnabled(snapshot.canRun);
  advancedGroup_->setEnabled(snapshot.canRun);

  if (snapshot.canCancel && snapshot.progress.total == 0)
  {
    progressBar_->setRange(0, 0);
  }
  else
  {
    progressBar_->setRange(0, 1000);
    const std::uint64_t total = snapshot.progress.total;
    const int value =
      snapshot.state == PolygonWorkspaceState::Completed
        ? 1000
        : (total == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(1000, snapshot.progress.completed * 1000 / total)));
    progressBar_->setValue(value);
  }

  const auto & objective = snapshot.objective;
  const auto & metrics = snapshot.metrics;
  metricsText_->setPlainText(tr("Solver: %1\nСтатус решения: %2\nРазмещено: %3 / %4\nUsed length: %5 мм\nПравая полоса: %6 мм\n"
                                "Доп. прямоугольник: %7 мм²\nФрагментация: %8 мм²\nUtilization: %9 %\n"
                                "Кандидаты: %10\nExpanded states: %11\nВремя: %12 мс")
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

  unplacedList_->clear();
  for (const std::string & instance : snapshot.unplacedInstances)
    unplacedList_->addItem(QString::fromStdString(instance));
  if (snapshot.unplacedInstances.empty())
    unplacedList_->addItem(tr("Все экземпляры размещены"));
  canvas_->setSnapshot(snapshot);
}
