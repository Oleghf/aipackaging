#include <algorithm>
#include <limits>
#include <QComboBox>
#include <QCoreApplication>
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
#include <stdexcept>

#include <polygoncanvaswidget.h>
#include <polygonworkspacewidget.h>

namespace
{
/// Преобразует внутренний статус базового алгоритма в подпись поля выбора.
QString solverTitle(BaselineAlgorithm kind)
{
  switch (kind)
  {
    case BaselineAlgorithm::InputFirstFit:
      return QCoreApplication::translate("PolygonWorkspaceWidget", "Первый допустимый вариант");
    case BaselineAlgorithm::AreaLeftBottom:
      return QCoreApplication::translate("PolygonWorkspaceWidget", "По площади, слева направо");
    case BaselineAlgorithm::MaxSideLeftBottom:
      return QCoreApplication::translate("PolygonWorkspaceWidget", "По габариту, слева направо");
    case BaselineAlgorithm::RandomLeftBottom:
      return QCoreApplication::translate("PolygonWorkspaceWidget", "Случайный поиск");
    case BaselineAlgorithm::Beam:
      return QCoreApplication::translate("PolygonWorkspaceWidget", "Лучевой поиск");
  }
  return {};
}

/// Добавляет вариант решателя с именем формата обмена в пользовательские данные.
void addSolver(QComboBox * box, BaselineAlgorithm kind)
{
  box->addItem(solverTitle(kind));
  const int index = box->count() - 1;
  box->setItemData(index, static_cast<int>(NestingMethod::Baseline), Qt::UserRole + 1);
  box->setItemData(index, static_cast<int>(kind), Qt::UserRole + 2);
}

/// Добавляет нейросетевой вариант с явно заданным способом выбора.
void addNeuralSolver(QComboBox * box, const QString & title, NestingMethod method, NeuralSelectionMode selection)
{
  box->addItem(title);
  const int index = box->count() - 1;
  box->setItemData(index, static_cast<int>(method), Qt::UserRole + 1);
  box->setItemData(index, static_cast<int>(selection), Qt::UserRole + 3);
}
} // namespace

/// Собирает самостоятельную полигональную вкладку, не меняющую клеточный виджет.
PolygonWorkspaceWidget::PolygonWorkspaceWidget(QWidget * parent)
  : QWidget(parent)
  , canvas_(new PolygonCanvasWidget(this))
  , openButton_(new QPushButton(tr("Открыть задачу"), this))
  , saveButton_(new QPushButton(tr("Сохранить решение"), this))
  , startButton_(new QPushButton(tr("Запустить"), this))
  , cancelButton_(new QPushButton(tr("Отменить"), this))
  , fitButton_(new QPushButton(tr("Вписать лист"), this))
  , modelButton_(new QPushButton(tr("Загрузить модель"), this))
  , solverBox_(new QComboBox(this))
  , advancedToggle_(new QToolButton(this))
  , advancedGroup_(new QGroupBox(this))
  , seedEdit_(new QLineEdit(QStringLiteral("42"), advancedGroup_))
  , seedValidationLabel_(new QLabel(advancedGroup_))
  , randomIterationsSpin_(new QSpinBox(advancedGroup_))
  , beamWidthSpin_(new QSpinBox(advancedGroup_))
  , maxExpandedSpin_(new QSpinBox(advancedGroup_))
  , timeoutSpin_(new QSpinBox(advancedGroup_))
  , neuralRolloutsSpin_(new QSpinBox(advancedGroup_))
  , problemLabel_(new QLabel(tr("Задача: —"), this))
  , modelLabel_(new QLabel(tr("Модель: не загружена"), this))
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
  modelButton_->setObjectName("polygonModelButton");
  modelLabel_->setObjectName("polygonModelStatus");
  advancedToggle_->setObjectName("polygonAdvancedToggle");
  advancedGroup_->setObjectName("polygonAdvancedGroup");
  seedEdit_->setObjectName("polygonSeedEdit");
  seedValidationLabel_->setObjectName("polygonSeedValidation");
  progressBar_->setObjectName("polygonProgress");
  statusLabel_->setObjectName("polygonStatus");

  addSolver(solverBox_, BaselineAlgorithm::InputFirstFit);
  addSolver(solverBox_, BaselineAlgorithm::AreaLeftBottom);
  addSolver(solverBox_, BaselineAlgorithm::MaxSideLeftBottom);
  addSolver(solverBox_, BaselineAlgorithm::RandomLeftBottom);
  addSolver(solverBox_, BaselineAlgorithm::Beam);
  addNeuralSolver(solverBox_, tr("Нейросетевая: жадный режим"), NestingMethod::Neural, NeuralSelectionMode::Greedy);
  addNeuralSolver(solverBox_, tr("Нейросетевая: лучшее из прогонов"), NestingMethod::Neural, NeuralSelectionMode::BestOf);
  addNeuralSolver(solverBox_, tr("Гибридный режим"), NestingMethod::Hybrid, NeuralSelectionMode::BestOf);
  solverBox_->setCurrentIndex(1);

  seedEdit_->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{1,20}")), seedEdit_));
  for (QSpinBox * spin : {randomIterationsSpin_, beamWidthSpin_, maxExpandedSpin_})
    spin->setRange(1, 1'000'000);
  timeoutSpin_->setRange(0, 3'600'000);
  neuralRolloutsSpin_->setRange(1, 1024);
  randomIterationsSpin_->setValue(64);
  beamWidthSpin_->setValue(32);
  maxExpandedSpin_->setValue(50'000);
  timeoutSpin_->setValue(30'000);
  neuralRolloutsSpin_->setValue(16);

  QFormLayout * advancedLayout = new QFormLayout(advancedGroup_);
  advancedLayout->addRow(tr("Начальное значение"), seedEdit_);
  advancedLayout->addRow(QString(), seedValidationLabel_);
  advancedLayout->addRow(tr("Случайные итерации"), randomIterationsSpin_);
  advancedLayout->addRow(tr("Ширина луча"), beamWidthSpin_);
  advancedLayout->addRow(tr("Раскрываемые состояния"), maxExpandedSpin_);
  advancedLayout->addRow(tr("Ограничение времени, мс"), timeoutSpin_);
  advancedLayout->addRow(tr("Нейросетевые прогоны"), neuralRolloutsSpin_);
  advancedToggle_->setText(tr("Расширенные настройки"));
  advancedToggle_->setCheckable(true);
  advancedToggle_->setChecked(false);
  advancedToggle_->setArrowType(Qt::RightArrow);
  advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  advancedGroup_->setVisible(false);
  seedValidationLabel_->setStyleSheet(QStringLiteral("color:#B91C1C"));
  seedValidationLabel_->setWordWrap(true);

  metricsText_->setReadOnly(true);
  metricsText_->setMinimumHeight(165);
  unplacedList_->setMinimumHeight(100);
  statusLabel_->setWordWrap(true);

  QHBoxLayout * fileLayout = new QHBoxLayout();
  fileLayout->addWidget(openButton_);
  fileLayout->addWidget(saveButton_);
  fileLayout->addWidget(fitButton_);
  fileLayout->addWidget(modelButton_);

  QHBoxLayout * runLayout = new QHBoxLayout();
  runLayout->addWidget(solverBox_, 1);
  runLayout->addWidget(startButton_);
  runLayout->addWidget(cancelButton_);

  QWidget * side = new QWidget(this);
  side->setMinimumWidth(310);
  side->setMaximumWidth(380);
  QVBoxLayout * sideLayout = new QVBoxLayout(side);
  sideLayout->addWidget(problemLabel_);
  sideLayout->addWidget(modelLabel_);
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
  connect(modelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestOpenModel);
  connect(startButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestStart);
  connect(cancelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestCancel);
  connect(fitButton_, &QPushButton::clicked, canvas_, &PolygonCanvasWidget::fitToView);
  connect(advancedToggle_, &QToolButton::toggled, this,
          [this](bool expanded)
          {
            advancedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            advancedGroup_->setVisible(expanded);
          });
  connect(solverBox_, &QComboBox::currentIndexChanged, this,
          [this]()
          {
            const auto method = static_cast<NestingMethod>(solverBox_->currentData(Qt::UserRole + 1).toInt());
            timeoutSpin_->setValue(method == NestingMethod::Baseline ? 30'000 : 300'000);
            updateStartAvailability();
          });
  connect(seedEdit_, &QLineEdit::textChanged, this, [this]() { updateStartAvailability(); });

  present({});
}

/// Разбирает имя решателя формата обмена и значения конфигурации из виджетов.
NestingRunRequest PolygonWorkspaceWidget::solverConfig() const
{
  if (!settingsValid())
    throw std::invalid_argument("Начальное значение выходит за диапазон 64-разрядного беззнакового целого");
  NestingRunRequest result;
  result.method = static_cast<NestingMethod>(solverBox_->currentData(Qt::UserRole + 1).toInt());
  result.algorithm = static_cast<BaselineAlgorithm>(solverBox_->currentData(Qt::UserRole + 2).toInt());
  result.neuralSelection = static_cast<NeuralSelectionMode>(solverBox_->currentData(Qt::UserRole + 3).toInt());
  bool seedValid = false;
  result.seed = seedEdit_->text().toULongLong(&seedValid);
  result.randomIterations = static_cast<std::size_t>(randomIterationsSpin_->value());
  result.beamWidth = static_cast<std::size_t>(beamWidthSpin_->value());
  result.maxExpandedStates = static_cast<std::size_t>(maxExpandedSpin_->value());
  result.timeoutMs = static_cast<std::uint64_t>(timeoutSpin_->value());
  result.neuralRollouts = static_cast<std::size_t>(neuralRolloutsSpin_->value());
  return result;
}

/// Проверяет непустой десятичный ввод штатным преобразованием Qt во весь диапазон `uint64_t`.
bool PolygonWorkspaceWidget::settingsValid() const
{
  bool valid = false;
  seedEdit_->text().toULongLong(&valid);
  return valid && !seedEdit_->text().isEmpty();
}

/// Согласует кнопку запуска и диагностику начального значения с последним снимком.
void PolygonWorkspaceWidget::updateStartAvailability()
{
  const bool valid = settingsValid();
  seedValidationLabel_->setText(valid ? QString() : tr("Введите целое число от 0 до 18446744073709551615"));
  seedValidationLabel_->setVisible(!valid);
  seedEdit_->setStyleSheet(valid ? QString() : QStringLiteral("border:1px solid #B91C1C"));
  const auto method = static_cast<NestingMethod>(solverBox_->currentData(Qt::UserRole + 1).toInt());
  const bool methodAvailable = method == NestingMethod::Baseline || solverBox_->property("modelReady").toBool();
  startButton_->setEnabled(solverBox_->property("workspaceCanRun").toBool() && methodAvailable && valid);
}

/// Обновляет все поля из одного снимка, исключая противоречивые состояния кнопок.
void PolygonWorkspaceWidget::present(const PolygonWorkspaceSnapshot & snapshot)
{
  problemLabel_->setText(snapshot.problemId.empty() ? tr("Задача: —")
                                                    : tr("Задача: %1").arg(QString::fromStdString(snapshot.problemId)));
  statusLabel_->setText(QString::fromStdString(snapshot.statusText));
  const QString modelHash = snapshot.modelSha256.empty() ? QString() : QString::fromStdString(snapshot.modelSha256.substr(0, 12));
  modelLabel_->setText(
    snapshot.modelReady
      ? tr("Модель: %1 (%2)").arg(QString::fromStdString(snapshot.modelId), modelHash)
      : tr("Модель: не загружена%1")
          .arg(snapshot.modelStatusText.empty() ? QString() : tr(" — %1").arg(QString::fromStdString(snapshot.modelStatusText))));
  statusLabel_->setStyleSheet(snapshot.partial ? QStringLiteral("color:#B45309;font-weight:600") : QString());
  openButton_->setEnabled(snapshot.canOpen);
  saveButton_->setEnabled(snapshot.canSave);
  solverBox_->setProperty("workspaceCanRun", snapshot.canRun);
  solverBox_->setProperty("modelReady", snapshot.modelReady);
  updateStartAvailability();
  cancelButton_->setEnabled(snapshot.canCancel);
  solverBox_->setEnabled(snapshot.canRun);
  advancedToggle_->setEnabled(snapshot.canRun);
  advancedGroup_->setEnabled(snapshot.canRun);
  modelButton_->setEnabled(snapshot.canLoadModel);

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

  unplacedList_->clear();
  for (const std::string & instance : snapshot.unplacedInstances)
    unplacedList_->addItem(QString::fromStdString(instance));
  if (snapshot.unplacedInstances.empty())
    unplacedList_->addItem(tr("Все экземпляры размещены"));
  canvas_->setSnapshot(snapshot);
}
