#include <algorithm>
#include <cstdint>
#include <limits>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
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
#include <QTreeWidget>
#include <QVBoxLayout>
#include <stdexcept>

#include <polygoncanvaswidget.h>
#include <polygonworkspacewidget.h>

namespace
{
enum RunPreset : std::uint8_t
{
  FastPreset = 0,
  QualityPreset = 1,
  NeuralPreset = 2,
  HybridPreset = 3,
  CustomPreset = 4
};

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
  , cancelModelButton_(new QPushButton(tr("Отменить проверку модели"), this))
  , solverBox_(new QComboBox(this))
  , expertSolverBox_(new QComboBox(this))
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
  , partTree_(new QTreeWidget(this))
  , partDetailsLabel_(new QLabel(tr("Выберите деталь в списке или на листе"), this))
  , stageLabel_(new QLabel(this))
  , splitter_(new QSplitter(Qt::Horizontal, this))
  , startAction_(new QAction(tr("Запустить раскрой"), this))
  , cancelAction_(new QAction(tr("Отменить расчёт"), this))
  , fitAction_(new QAction(tr("Вписать лист"), this))
{
  setObjectName("polygonWorkspace");
  canvas_->setObjectName("polygonCanvas");
  openButton_->setObjectName("polygonOpenButton");
  saveButton_->setObjectName("polygonSaveButton");
  startButton_->setObjectName("polygonStartButton");
  cancelButton_->setObjectName("polygonCancelButton");
  solverBox_->setObjectName("polygonSolverBox");
  modelButton_->setObjectName("polygonModelButton");
  cancelModelButton_->setObjectName("polygonCancelModelButton");
  modelLabel_->setObjectName("polygonModelStatus");
  advancedToggle_->setObjectName("polygonAdvancedToggle");
  advancedGroup_->setObjectName("polygonAdvancedGroup");
  seedEdit_->setObjectName("polygonSeedEdit");
  seedValidationLabel_->setObjectName("polygonSeedValidation");
  progressBar_->setObjectName("polygonProgress");
  statusLabel_->setObjectName("polygonStatus");
  solverBox_->setAccessibleName(tr("Режим раскроя"));
  solverBox_->setToolTip(tr("Выберите понятный режим; точные параметры доступны в расширенных настройках"));
  startButton_->setAccessibleName(tr("Запустить раскрой"));
  cancelButton_->setAccessibleName(tr("Отменить текущий расчёт"));
  modelButton_->setAccessibleName(tr("Подключить нейросетевую модель"));
  advancedToggle_->setAccessibleName(tr("Показать или скрыть расширенные настройки"));
  canvas_->setAccessibleName(tr("Полотно раскладки"));

  solverBox_->addItems(
    {tr("Быстрый"), tr("Качественный"), tr("Нейросетевой"), tr("Рекомендуемый гибридный"), tr("Пользовательские настройки")});
  addSolver(expertSolverBox_, BaselineAlgorithm::InputFirstFit);
  addSolver(expertSolverBox_, BaselineAlgorithm::AreaLeftBottom);
  addSolver(expertSolverBox_, BaselineAlgorithm::MaxSideLeftBottom);
  addSolver(expertSolverBox_, BaselineAlgorithm::RandomLeftBottom);
  addSolver(expertSolverBox_, BaselineAlgorithm::Beam);
  addNeuralSolver(expertSolverBox_, tr("Нейросетевой, жадный"), NestingMethod::Neural, NeuralSelectionMode::Greedy);
  addNeuralSolver(expertSolverBox_, tr("Нейросетевой, несколько прогонов"), NestingMethod::Neural, NeuralSelectionMode::BestOf);
  addNeuralSolver(expertSolverBox_, tr("Гибридный"), NestingMethod::Hybrid, NeuralSelectionMode::BestOf);
  solverBox_->setCurrentIndex(FastPreset);
  expertSolverBox_->setCurrentIndex(1);

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
  advancedLayout->addRow(tr("Внутренний алгоритм"), expertSolverBox_);
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
  partDetailsLabel_->setWordWrap(true);
  partTree_->setObjectName(QStringLiteral("polygonPartTree"));
  partTree_->setHeaderLabels({tr("Деталь"), tr("Количество")});
  partTree_->header()->setStretchLastSection(false);
  partTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  partTree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  stageLabel_->setObjectName(QStringLiteral("polygonProgressStage"));
  expertSolverBox_->setObjectName(QStringLiteral("polygonExpertSolverBox"));
  splitter_->setObjectName(QStringLiteral("workspaceSplitter"));
  startAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
  cancelAction_->setShortcut(QKeySequence(Qt::Key_Escape));
  fitAction_->setShortcut(QKeySequence(Qt::Key_F));
  startAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  cancelAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  fitAction_->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  addAction(startAction_);
  addAction(cancelAction_);
  addAction(fitAction_);
  openButton_->hide();
  saveButton_->hide();

  QHBoxLayout * runLayout = new QHBoxLayout();
  runLayout->addWidget(solverBox_, 1);
  runLayout->addWidget(startButton_);
  runLayout->addWidget(cancelButton_);

  auto * left = new QWidget(this);
  left->setMinimumWidth(240);
  auto * leftLayout = new QVBoxLayout(left);
  leftLayout->addWidget(problemLabel_);
  leftLayout->addWidget(new QLabel(tr("Состав задачи"), left));
  leftLayout->addWidget(partTree_, 1);
  leftLayout->addWidget(new QLabel(tr("Выбранная деталь"), left));
  leftLayout->addWidget(partDetailsLabel_);
  auto * legend = new QLabel(
    tr("Обозначения: цвет — тип детали; белая область — отверстие; пунктир — отступ; зелёная область — полезный остаток."), left);
  legend->setObjectName(QStringLiteral("polygonLegend"));
  legend->setWordWrap(true);
  leftLayout->addWidget(legend);

  auto * side = new QWidget(this);
  side->setMinimumWidth(300);
  auto * sideLayout = new QVBoxLayout(side);
  sideLayout->addWidget(new QLabel(tr("Режим раскроя"), side));
  sideLayout->addWidget(solverBox_);
  sideLayout->addLayout(runLayout);
  sideLayout->addWidget(modelLabel_);
  sideLayout->addWidget(modelButton_);
  sideLayout->addWidget(cancelModelButton_);
  sideLayout->addWidget(fitButton_);
  sideLayout->addWidget(advancedToggle_);
  sideLayout->addWidget(advancedGroup_);
  sideLayout->addWidget(new QLabel(tr("Не размещены"), side));
  sideLayout->addWidget(unplacedList_, 1);

  splitter_->addWidget(left);
  splitter_->addWidget(canvas_);
  splitter_->addWidget(side);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setStretchFactor(2, 0);
  splitter_->setSizes({250, 720, 320});

  auto * messages = new QWidget(this);
  auto * messagesLayout = new QVBoxLayout(messages);
  messagesLayout->setContentsMargins(0, 0, 0, 0);
  messagesLayout->addWidget(statusLabel_);
  messagesLayout->addWidget(stageLabel_);
  messagesLayout->addWidget(progressBar_);
  metricsText_->setMaximumHeight(130);
  messagesLayout->addWidget(metricsText_);

  QVBoxLayout * root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->addWidget(splitter_, 1);
  root->addWidget(messages);

  connect(openButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestOpenProblem);
  connect(saveButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestSaveSolution);
  connect(modelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestOpenModel);
  connect(cancelModelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestCancelModelLoad);
  connect(startButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestStart);
  connect(cancelButton_, &QPushButton::clicked, this, &PolygonWorkspaceWidget::requestCancel);
  connect(fitButton_, &QPushButton::clicked, canvas_, &PolygonCanvasWidget::fitToView);
  connect(startAction_, &QAction::triggered, this, &PolygonWorkspaceWidget::requestStart);
  connect(cancelAction_, &QAction::triggered, this, &PolygonWorkspaceWidget::requestCancel);
  connect(fitAction_, &QAction::triggered, canvas_, &PolygonCanvasWidget::fitToView);
  connect(advancedToggle_, &QToolButton::toggled, this,
          [this](bool expanded)
          {
            advancedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            advancedGroup_->setVisible(expanded);
          });
  connect(solverBox_, &QComboBox::currentIndexChanged, this,
          [this]()
          {
            if (!applyingPreset_)
              applyPreset(solverBox_->currentIndex());
            updateStartAvailability();
          });
  const auto markCustom = [this]()
  {
    if (!applyingPreset_)
      solverBox_->setCurrentIndex(CustomPreset);
    updateStartAvailability();
  };
  connect(expertSolverBox_, &QComboBox::currentIndexChanged, this, markCustom);
  connect(randomIterationsSpin_, &QSpinBox::valueChanged, this, markCustom);
  connect(beamWidthSpin_, &QSpinBox::valueChanged, this, markCustom);
  connect(maxExpandedSpin_, &QSpinBox::valueChanged, this, markCustom);
  connect(timeoutSpin_, &QSpinBox::valueChanged, this, markCustom);
  connect(neuralRolloutsSpin_, &QSpinBox::valueChanged, this, markCustom);
  connect(seedEdit_, &QLineEdit::textChanged, this,
          [this]()
          {
            if (!applyingPreset_)
              solverBox_->setCurrentIndex(CustomPreset);
            updateStartAvailability();
          });
  connect(canvas_, &PolygonCanvasWidget::cursorPositionChanged, this, &PolygonWorkspaceWidget::cursorPositionChanged);
  connect(canvas_, &PolygonCanvasWidget::partSelected, this, &PolygonWorkspaceWidget::showSelectedPart);
  connect(partTree_, &QTreeWidget::itemSelectionChanged, this,
          [this]()
          {
            if (const auto * item = partTree_->currentItem())
              showSelectedPart(item->data(0, Qt::UserRole).toString(), 0);
          });

  setTabOrder(solverBox_, startButton_);
  setTabOrder(startButton_, cancelButton_);
  setTabOrder(cancelButton_, modelButton_);
  setTabOrder(modelButton_, advancedToggle_);

  present({});
}

/// Разбирает имя решателя формата обмена и значения конфигурации из виджетов.
NestingRunRequest PolygonWorkspaceWidget::solverConfig() const
{
  if (!settingsValid())
    throw std::invalid_argument("Начальное значение выходит за диапазон 64-разрядного беззнакового целого");
  NestingRunRequest result;
  result.method = static_cast<NestingMethod>(expertSolverBox_->currentData(Qt::UserRole + 1).toInt());
  result.algorithm = static_cast<BaselineAlgorithm>(expertSolverBox_->currentData(Qt::UserRole + 2).toInt());
  result.neuralSelection = static_cast<NeuralSelectionMode>(expertSolverBox_->currentData(Qt::UserRole + 3).toInt());
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
  const auto method = static_cast<NestingMethod>(expertSolverBox_->currentData(Qt::UserRole + 1).toInt());
  const bool methodAvailable = method == NestingMethod::Baseline || solverBox_->property("modelReady").toBool();
  const bool enabled = solverBox_->property("workspaceCanRun").toBool() && methodAvailable && valid;
  startButton_->setEnabled(enabled);
  startAction_->setEnabled(enabled);
}

/// Обновляет все поля из одного снимка, исключая противоречивые состояния кнопок.
void PolygonWorkspaceWidget::present(const PolygonWorkspaceSnapshot & snapshot)
{
  snapshot_ = snapshot;
  problemLabel_->setText(snapshot.problemId.empty() ? tr("Задача: —")
                                                    : tr("Задача: %1\nЛист: %2 × %3 мм\nОтступ: %4 мм; зазор: %5 мм; рез: %6 мм")
                                                        .arg(QString::fromStdString(snapshot.problemId))
                                                        .arg(snapshot.document.sheetWidth, 0, 'f', 2)
                                                        .arg(snapshot.document.sheetHeight, 0, 'f', 2)
                                                        .arg(snapshot.document.sheetMargin, 0, 'f', 2)
                                                        .arg(snapshot.document.partSpacing, 0, 'f', 2)
                                                        .arg(snapshot.document.kerf, 0, 'f', 2));
  statusLabel_->setText(QString::fromStdString(snapshot.statusText));
  const QString modelHash = snapshot.modelSha256.empty() ? QString() : QString::fromStdString(snapshot.modelSha256.substr(0, 12));
  modelLabel_->setText(
    snapshot.modelState == PolygonModelState::Loading ? tr("Модель: выполняется проверка…")
    : snapshot.modelReady
      ? tr("Модель: %1 (%2)%3")
          .arg(QString::fromStdString(snapshot.modelId), modelHash,
               snapshot.modelStatusText.empty() ? QString() : tr(" — %1").arg(QString::fromStdString(snapshot.modelStatusText)))
      : tr("Модель: не загружена%1")
          .arg(snapshot.modelStatusText.empty() ? QString() : tr(" — %1").arg(QString::fromStdString(snapshot.modelStatusText))));
  statusLabel_->setStyleSheet(snapshot.partial ? QStringLiteral("color:#B45309;font-weight:600") : QString());
  openButton_->setEnabled(snapshot.canOpen);
  saveButton_->setEnabled(snapshot.canSave);
  solverBox_->setProperty("workspaceCanRun", snapshot.canRun);
  solverBox_->setProperty("modelReady", snapshot.modelReady);
  updateStartAvailability();
  cancelButton_->setEnabled(snapshot.canCancel);
  cancelAction_->setEnabled(snapshot.canCancel);
  solverBox_->setEnabled(snapshot.canRun);
  advancedToggle_->setEnabled(snapshot.canRun);
  advancedGroup_->setEnabled(snapshot.canRun);
  modelButton_->setEnabled(snapshot.canLoadModel);
  cancelModelButton_->setVisible(snapshot.canCancelModelLoad);
  cancelModelButton_->setEnabled(snapshot.canCancelModelLoad);

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

  unplacedList_->clear();
  for (const std::string & instance : snapshot.unplacedInstances)
    unplacedList_->addItem(QString::fromStdString(instance));
  if (snapshot.unplacedInstances.empty())
    unplacedList_->addItem(tr("Все экземпляры размещены"));
  partTree_->clear();
  for (const PolygonPartSummary & part : snapshot.document.parts)
  {
    auto * item = new QTreeWidgetItem(partTree_);
    item->setText(0, QString::fromStdString(part.id));
    item->setText(1, QString::number(part.quantity));
    item->setData(0, Qt::UserRole, QString::fromStdString(part.id));
  }
  canvas_->setSnapshot(snapshot);
}

/// Возвращает общую команду запуска для кнопки и панели главного окна.
QAction * PolygonWorkspaceWidget::startAction() const
{
  return startAction_;
}

/// Возвращает общую команду отмены для кнопки и панели главного окна.
QAction * PolygonWorkspaceWidget::cancelAction() const
{
  return cancelAction_;
}

/// Возвращает общую команду вписывания листа.
QAction * PolygonWorkspaceWidget::fitAction() const
{
  return fitAction_;
}

/// Выбирает гибридный режим, сохраняя все закреплённые бюджеты.
void PolygonWorkspaceWidget::selectRecommendedMode()
{
  solverBox_->setCurrentIndex(HybridPreset);
}

/// Ограничивает восстановленный индекс известными пользовательскими режимами.
void PolygonWorkspaceWidget::selectPreset(int preset)
{
  solverBox_->setCurrentIndex(std::clamp(preset, static_cast<int>(FastPreset), static_cast<int>(HybridPreset)));
}

/// Возвращает стабильный индекс выбранного режима без внутренних перечислений решателя.
int PolygonWorkspaceWidget::selectedPreset() const
{
  return solverBox_->currentIndex() == CustomPreset ? FastPreset : solverBox_->currentIndex();
}

/// Согласованно переключает кнопку и область экспертных параметров.
void PolygonWorkspaceWidget::setAdvancedExpanded(bool expanded)
{
  advancedToggle_->setChecked(expanded);
}

/// Возвращает сохранённое пользователем состояние экспертной области.
bool PolygonWorkspaceWidget::advancedExpanded() const
{
  return advancedToggle_->isChecked();
}

/// Сериализует положение трёх рабочих панелей средствами Qt.
QByteArray PolygonWorkspaceWidget::splitterState() const
{
  return splitter_->saveState();
}

/// Применяет только непустое корректное состояние внутренних панелей.
void PolygonWorkspaceWidget::restoreSplitterState(const QByteArray & state)
{
  if (!state.isEmpty())
    splitter_->restoreState(state);
}

/// Заполняет экспертный запрос закреплёнными значениями понятного режима.
void PolygonWorkspaceWidget::applyPreset(int preset)
{
  if (preset == CustomPreset)
    return;
  applyingPreset_ = true;
  randomIterationsSpin_->setValue(64);
  beamWidthSpin_->setValue(32);
  maxExpandedSpin_->setValue(50'000);
  neuralRolloutsSpin_->setValue(16);
  switch (preset)
  {
    case FastPreset:
      expertSolverBox_->setCurrentIndex(1);
      timeoutSpin_->setValue(30'000);
      break;
    case QualityPreset:
      expertSolverBox_->setCurrentIndex(4);
      timeoutSpin_->setValue(30'000);
      break;
    case NeuralPreset:
      expertSolverBox_->setCurrentIndex(6);
      timeoutSpin_->setValue(300'000);
      break;
    case HybridPreset:
      expertSolverBox_->setCurrentIndex(7);
      timeoutSpin_->setValue(300'000);
      break;
    default:
      break;
  }
  applyingPreset_ = false;
}

/// Находит тип и размещение выбранного экземпляра и показывает измерения в миллиметрах.
void PolygonWorkspaceWidget::showSelectedPart(const QString & partId, std::uint32_t instanceIndex)
{
  const auto found =
    std::find_if(snapshot_.document.parts.begin(), snapshot_.document.parts.end(),
                 [&partId](const PolygonPartSummary & part) { return QString::fromStdString(part.id) == partId; });
  if (found == snapshot_.document.parts.end())
    return;
  QStringList rotations;
  for (int rotation : found->allowedRotations)
    rotations.push_back(tr("%1°").arg(rotation));
  int placedRotation = -1;
  for (const PolygonPlacedPartView & placement : snapshot_.scene.placements)
  {
    if (QString::fromStdString(placement.partId) == partId && placement.instanceIndex == instanceIndex)
    {
      placedRotation = placement.rotationDegrees;
      break;
    }
  }
  partDetailsLabel_->setText(tr("%1, экземпляр %2\nКоличество: %3\nГабариты: %4 × %5 мм\nПлощадь: %6 мм²\nПовороты: %7%8")
                               .arg(partId)
                               .arg(instanceIndex)
                               .arg(found->quantity)
                               .arg(found->width, 0, 'f', 2)
                               .arg(found->height, 0, 'f', 2)
                               .arg(found->materialArea, 0, 'f', 2)
                               .arg(rotations.join(QStringLiteral(", ")))
                               .arg(placedRotation >= 0 ? tr("\nРазмещённый поворот: %1°").arg(placedRotation) : QString()));
}
