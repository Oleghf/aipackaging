#include <algorithm>
#include <cstdint>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <stdexcept>

#include <polygonrunpanel.h>

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

/// Преобразует внутренний вид базового алгоритма в понятную пользователю подпись.
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

/// Добавляет базовый алгоритм и сохраняет его прикладные перечисления в данных элемента.
void addSolver(QComboBox * box, BaselineAlgorithm kind)
{
  box->addItem(solverTitle(kind));
  const int index = box->count() - 1;
  box->setItemData(index, static_cast<int>(NestingMethod::Baseline), Qt::UserRole + 1);
  box->setItemData(index, static_cast<int>(kind), Qt::UserRole + 2);
}

/// Добавляет нейросетевой вариант и сохраняет способ выбора в данных элемента.
void addNeuralSolver(QComboBox * box, const QString & title, NestingMethod method, NeuralSelectionMode selection)
{
  box->addItem(title);
  const int index = box->count() - 1;
  box->setItemData(index, static_cast<int>(method), Qt::UserRole + 1);
  box->setItemData(index, static_cast<int>(selection), Qt::UserRole + 3);
}
} // namespace

/// Создаёт элементы, компоновку, действия и соединения правой панели.
PolygonRunPanel::PolygonRunPanel(QWidget * parent)
  : QWidget(parent)
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
  , modelLabel_(new QLabel(tr("Модель: не загружена"), this))
  , unplacedList_(new QListWidget(this))
  , startAction_(new QAction(tr("Запустить раскрой"), this))
  , cancelAction_(new QAction(tr("Отменить расчёт"), this))
  , fitAction_(new QAction(tr("Вписать лист"), this))
{
  setMinimumWidth(300);
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
  expertSolverBox_->setObjectName(QStringLiteral("polygonExpertSolverBox"));
  solverBox_->setAccessibleName(tr("Режим раскроя"));
  solverBox_->setToolTip(tr("Выберите понятный режим; точные параметры доступны в расширенных настройках"));
  startButton_->setAccessibleName(tr("Запустить раскрой"));
  cancelButton_->setAccessibleName(tr("Отменить текущий расчёт"));
  modelButton_->setAccessibleName(tr("Подключить нейросетевую модель"));
  advancedToggle_->setAccessibleName(tr("Показать или скрыть расширенные настройки"));

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

  auto * advancedLayout = new QFormLayout(advancedGroup_);
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
  unplacedList_->setMinimumHeight(100);

  auto * runLayout = new QHBoxLayout();
  runLayout->addWidget(startButton_);
  runLayout->addWidget(cancelButton_);
  auto * layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(tr("Режим раскроя"), this));
  layout->addWidget(solverBox_);
  layout->addLayout(runLayout);
  layout->addWidget(modelLabel_);
  layout->addWidget(modelButton_);
  layout->addWidget(cancelModelButton_);
  layout->addWidget(fitButton_);
  layout->addWidget(advancedToggle_);
  layout->addWidget(advancedGroup_);
  layout->addWidget(new QLabel(tr("Не размещены"), this));
  layout->addWidget(unplacedList_, 1);

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

  connect(openButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestOpenProblem);
  connect(saveButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestSaveSolution);
  connect(modelButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestOpenModel);
  connect(cancelModelButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestCancelModelLoad);
  connect(startButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestStart);
  connect(cancelButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestCancel);
  connect(fitButton_, &QPushButton::clicked, this, &PolygonRunPanel::requestFit);
  connect(startAction_, &QAction::triggered, this, &PolygonRunPanel::requestStart);
  connect(cancelAction_, &QAction::triggered, this, &PolygonRunPanel::requestCancel);
  connect(fitAction_, &QAction::triggered, this, &PolygonRunPanel::requestFit);
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

  setTabOrder(solverBox_, startButton_);
  setTabOrder(startButton_, cancelButton_);
  setTabOrder(cancelButton_, modelButton_);
  setTabOrder(modelButton_, advancedToggle_);
}

/// Разбирает перечисления и числовые поля панели после проверки начального значения.
NestingRunRequest PolygonRunPanel::solverConfig() const
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
bool PolygonRunPanel::settingsValid() const
{
  bool valid = false;
  seedEdit_->text().toULongLong(&valid);
  return valid && !seedEdit_->text().isEmpty();
}

/// Обновляет модель, доступность команд и список неразмещённых экземпляров из одного снимка.
void PolygonRunPanel::present(const PolygonWorkspaceSnapshot & snapshot)
{
  const QString modelHash = snapshot.modelSha256.empty() ? QString() : QString::fromStdString(snapshot.modelSha256.substr(0, 12));
  modelLabel_->setText(
    snapshot.modelState == PolygonModelState::Loading ? tr("Модель: выполняется проверка…")
    : snapshot.modelReady
      ? tr("Модель: %1 (%2)%3")
          .arg(QString::fromStdString(snapshot.modelId), modelHash,
               snapshot.modelStatusText.empty() ? QString() : tr(" — %1").arg(QString::fromStdString(snapshot.modelStatusText)))
      : tr("Модель: не загружена%1")
          .arg(snapshot.modelStatusText.empty() ? QString() : tr(" — %1").arg(QString::fromStdString(snapshot.modelStatusText))));

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

  unplacedList_->clear();
  for (const std::string & instance : snapshot.unplacedInstances)
    unplacedList_->addItem(QString::fromStdString(instance));
  if (snapshot.unplacedInstances.empty())
    unplacedList_->addItem(tr("Все экземпляры размещены"));
}

/// Возвращает действие запуска, принадлежащее панели.
QAction * PolygonRunPanel::startAction() const
{
  return startAction_;
}

/// Возвращает действие отмены, принадлежащее панели.
QAction * PolygonRunPanel::cancelAction() const
{
  return cancelAction_;
}

/// Возвращает действие вписывания листа, принадлежащее панели.
QAction * PolygonRunPanel::fitAction() const
{
  return fitAction_;
}

/// Выбирает закреплённый рекомендуемый гибридный режим.
void PolygonRunPanel::selectRecommendedMode()
{
  solverBox_->setCurrentIndex(HybridPreset);
}

/// Ограничивает восстановленный индекс диапазоном понятных пользовательских режимов.
void PolygonRunPanel::selectPreset(int preset)
{
  solverBox_->setCurrentIndex(std::clamp(preset, static_cast<int>(FastPreset), static_cast<int>(HybridPreset)));
}

/// Скрывает внутренний пользовательский режим за стабильным быстрым значением.
int PolygonRunPanel::selectedPreset() const
{
  return solverBox_->currentIndex() == CustomPreset ? FastPreset : solverBox_->currentIndex();
}

/// Переключает кнопку и область экспертных параметров одним сигналом Qt.
void PolygonRunPanel::setAdvancedExpanded(bool expanded)
{
  advancedToggle_->setChecked(expanded);
}

/// Возвращает состояние кнопки раскрытия экспертных параметров.
bool PolygonRunPanel::advancedExpanded() const
{
  return advancedToggle_->isChecked();
}

/// Проверяет модель и начальное значение перед включением запуска.
void PolygonRunPanel::updateStartAvailability()
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

/// Записывает закреплённые бюджеты и внутренний алгоритм без промежуточного перехода в пользовательский режим.
void PolygonRunPanel::applyPreset(int preset)
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
