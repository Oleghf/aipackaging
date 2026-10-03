#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>
#include <tuple>

#include <polygondxfimportwizard.h>

namespace
{
constexpr int IDENTIFIER_ROLE = Qt::UserRole + 1;
} // namespace

/// Рисует нейтральный предпросмотр габаритов распознанных цепочек.
class DxfPathPreview final : public QWidget
{
public:
  /// Создаёт область с минимальной высотой для страницы мастера.
  explicit DxfPathPreview(QWidget * parent = nullptr)
    : QWidget(parent)
  {
    setMinimumHeight(180);
    setAccessibleName(tr("Предпросмотр распознанных цепочек"));
  }

  /// Заменяет набор цепочек и запрашивает перерисовку.
  void setPaths(std::vector<PolygonImportPath> paths)
  {
    paths_ = std::move(paths);
    update();
  }

protected:
  /// Вписывает общие габариты и различает замкнутые и открытые цепочки цветом.
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());
    if (paths_.empty())
      return;
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto & path : paths_)
    {
      minX = std::min(minX, path.minX);
      minY = std::min(minY, path.minY);
      maxX = std::max(maxX, path.maxX);
      maxY = std::max(maxY, path.maxY);
    }
    const double spanX = std::max(1e-9, maxX - minX);
    const double spanY = std::max(1e-9, maxY - minY);
    const QRectF area = rect().adjusted(12, 12, -12, -12);
    const double scale = std::min(area.width() / spanX, area.height() / spanY);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto & path : paths_)
    {
      const QRectF box(area.left() + (path.minX - minX) * scale, area.bottom() - (path.maxY - minY) * scale,
                       std::max(1.0, (path.maxX - path.minX) * scale), std::max(1.0, (path.maxY - path.minY) * scale));
      QPen pen(path.closed ? QColor(30, 130, 70) : QColor(190, 90, 30), 2.0);
      if (!path.closed)
        pen.setStyle(Qt::DashLine);
      painter.setPen(pen);
      painter.drawRect(box);
    }
  }

private:
  std::vector<PolygonImportPath> paths_;
};

namespace
{

/// Создаёт числовое поле миллиметров с едиными пределами и точностью.
QDoubleSpinBox * millimeterField(QWidget * parent, double minimum = 0.0, double maximum = 1'000'000.0)
{
  auto * field = new QDoubleSpinBox(parent);
  field->setRange(minimum, maximum);
  field->setDecimals(3);
  field->setSuffix(QObject::tr(" мм"));
  return field;
}

/// Разбирает строку разрешённых прямоугольных поворотов без принятия иных значений.
std::vector<int> rotationsFromText(const QString & text)
{
  std::vector<int> result;
  for (const QString & token : text.split(',', Qt::SkipEmptyParts))
  {
    bool ok = false;
    const int value = token.trimmed().toInt(&ok);
    if (ok && (value == 0 || value == 90 || value == 180 || value == 270) &&
        std::find(result.begin(), result.end(), value) == result.end())
      result.push_back(value);
  }
  if (result.empty())
    result.push_back(0);
  return result;
}
} // namespace

/// Создаёт элементы управления и сохраняет штатное поведение клавиш `QWizard`.
PolygonDxfImportWizard::PolygonDxfImportWizard(QWidget * parent)
  : QWizard(parent)
  , statusLabel_(new QLabel(this))
  , unitCombo_(new QComboBox(this))
  , joinTolerance_(millimeterField(this, 0.0, 1.0))
  , reanalyzeButton_(new QPushButton(tr("Пересчитать цепочки"), this))
  , layersList_(new QListWidget(this))
  , ignoreUnsupported_(new QCheckBox(tr("Подтверждаю пропуск неподдерживаемых сущностей выбранных слоёв"), this))
  , joinsList_(new QListWidget(this))
  , assignmentsTable_(new QTableWidget(this))
  , preview_(new DxfPathPreview(this))
  , partsTable_(new QTableWidget(this))
  , groupDuplicates_(new QCheckBox(tr("Объединить подтверждённые одинаковые детали"), this))
  , problemId_(new QLineEdit(this))
  , sheetWidth_(millimeterField(this))
  , sheetHeight_(millimeterField(this))
  , sheetMargin_(millimeterField(this))
  , partSpacing_(millimeterField(this))
  , kerf_(millimeterField(this))
  , curveTolerance_(millimeterField(this, 0.001, 0.05))
  , resultLabel_(new QLabel(this))
{
  setObjectName(QStringLiteral("polygonDxfImportWizard"));
  setWindowTitle(tr("Импорт ASCII DXF"));
  setButtonText(QWizard::BackButton, tr("Назад"));
  setButtonText(QWizard::NextButton, tr("Далее"));
  setButtonText(QWizard::FinishButton, tr("Построить документ"));
  setButtonText(QWizard::CancelButton, tr("Отмена"));
  setOption(QWizard::NoBackButtonOnStartPage);
  setMinimumSize(900, 650);
  buildPages();
  connect(reanalyzeButton_, &QPushButton::clicked, this, &PolygonDxfImportWizard::reanalyze);
  connect(this, &QWizard::currentIdChanged, this,
          [this](int page)
          {
            if (page == 3)
              rebuildParts();
          });
}

/// Сохраняет прикладные функции и начинает анализ без синхронного чтения в Qt.
void PolygonDxfImportWizard::startImport(QString filePath, PolygonImportActions actions)
{
  filePath_ = std::move(filePath);
  actions_ = std::move(actions);
  problemId_->setText(QFileInfo(filePath_).completeBaseName());
  if (actions_.inspect)
    actions_.inspect({filePath_.toStdString()});
}

/// Обновляет состояние мастера только согласованным прикладным снимком.
void PolygonDxfImportWizard::present(const PolygonImportSnapshot & snapshot)
{
  snapshot_ = snapshot;
  statusLabel_->setText(QString::fromStdString(snapshot.statusText));
  const bool busy = snapshot.state == PolygonImportState::Inspecting || snapshot.state == PolygonImportState::Building;
  button(QWizard::CancelButton)->setText(busy ? tr("Отменить") : tr("Закрыть"));
  reanalyzeButton_->setEnabled(!busy && !filePath_.isEmpty());
  if (snapshot.state == PolygonImportState::Ready && !inspectionPopulated_)
  {
    populateInspection(snapshot.inspection);
    inspectionPopulated_ = true;
  }
  if (snapshot.state == PolygonImportState::Inspecting)
    inspectionPopulated_ = false;
  if (snapshot.state == PolygonImportState::Completed)
  {
    QString text = snapshot.resultValid ? tr("Точная проверка пройдена. Документ можно принять как задачу.")
                                        : tr("Документ содержит ошибки и может быть принят только как черновик.");
    for (const auto & diagnostic : snapshot.documentDiagnostics)
      text += QStringLiteral("\n") + QString::fromStdString(diagnostic.message);
    resultLabel_->setText(text);
    button(QWizard::FinishButton)->setText(snapshot.resultValid ? tr("Принять задачу") : tr("Принять черновик"));
  }
  else
  {
    button(QWizard::FinishButton)
      ->setText(snapshot.state == PolygonImportState::Building ? tr("Проверка…") : tr("Построить документ"));
  }
  button(QWizard::FinishButton)
    ->setEnabled(!busy && (snapshot.state == PolygonImportState::Ready || snapshot.state == PolygonImportState::Completed ||
                           snapshot.canBuild));
  button(QWizard::NextButton)->setEnabled(!busy && snapshot.inspection.success);
  button(QWizard::BackButton)->setEnabled(!busy);
}

/// Создаёт страницы мастера и устойчивые имена элементов для автоматических проверок.
void PolygonDxfImportWizard::buildPages()
{
  auto * sourcePage = new QWizardPage(this);
  sourcePage->setTitle(tr("Файл и единицы"));
  unitCombo_->setObjectName(QStringLiteral("dxfUnitCombo"));
  const std::array units{std::pair{tr("миллиметры"), PolygonImportUnit::Millimeter},
                         std::pair{tr("сантиметры"), PolygonImportUnit::Centimeter},
                         std::pair{tr("метры"), PolygonImportUnit::Meter},
                         std::pair{tr("километры"), PolygonImportUnit::Kilometer},
                         std::pair{tr("дюймы"), PolygonImportUnit::Inch},
                         std::pair{tr("футы"), PolygonImportUnit::Foot},
                         std::pair{tr("ярды"), PolygonImportUnit::Yard},
                         std::pair{tr("микрометры"), PolygonImportUnit::Micrometer}};
  for (const auto & [title, value] : units)
    unitCombo_->addItem(title, static_cast<int>(value));
  joinTolerance_->setValue(0.01);
  joinTolerance_->setObjectName(QStringLiteral("dxfJoinTolerance"));
  statusLabel_->setWordWrap(true);
  auto * sourceForm = new QFormLayout(sourcePage);
  sourceForm->addRow(tr("Состояние:"), statusLabel_);
  sourceForm->addRow(tr("Единица длины:"), unitCombo_);
  sourceForm->addRow(tr("Допуск соединения:"), joinTolerance_);
  sourceForm->addRow(QString(), reanalyzeButton_);
  setPage(0, sourcePage);

  auto * layerPage = new QWizardPage(this);
  layerPage->setTitle(tr("Слои и неподдерживаемые сущности"));
  layersList_->setObjectName(QStringLiteral("dxfLayersList"));
  ignoreUnsupported_->setObjectName(QStringLiteral("dxfIgnoreUnsupported"));
  auto * layerLayout = new QVBoxLayout(layerPage);
  layerLayout->addWidget(new QLabel(tr("Выберите слои, геометрия которых должна попасть в документ:"), layerPage));
  layerLayout->addWidget(layersList_, 1);
  layerLayout->addWidget(ignoreUnsupported_);
  setPage(1, layerPage);

  auto * geometryPage = new QWizardPage(this);
  geometryPage->setTitle(tr("Цепочки, соединения и отверстия"));
  joinsList_->setObjectName(QStringLiteral("dxfJoinList"));
  assignmentsTable_->setObjectName(QStringLiteral("dxfAssignmentsTable"));
  assignmentsTable_->setColumnCount(4);
  assignmentsTable_->setHorizontalHeaderLabels({tr("Цепочка"), tr("Слой"), tr("Назначение"), tr("Внешнее кольцо")});
  assignmentsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  auto * geometryLayout = new QVBoxLayout(geometryPage);
  geometryLayout->addWidget(preview_, 1);
  geometryLayout->addWidget(new QLabel(tr("Предлагаемые соединения применяются только после отметки:"), geometryPage));
  geometryLayout->addWidget(joinsList_);
  geometryLayout->addWidget(assignmentsTable_, 2);
  setPage(2, geometryPage);

  auto * partPage = new QWizardPage(this);
  partPage->setTitle(tr("Типы деталей"));
  partsTable_->setObjectName(QStringLiteral("dxfPartsTable"));
  partsTable_->setColumnCount(4);
  partsTable_->setHorizontalHeaderLabels({tr("Цепочка"), tr("Идентификатор"), tr("Количество"), tr("Повороты")});
  partsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  auto * partLayout = new QVBoxLayout(partPage);
  partLayout->addWidget(partsTable_, 1);
  partLayout->addWidget(groupDuplicates_);
  setPage(3, partPage);

  auto * taskPage = new QWizardPage(this);
  taskPage->setTitle(tr("Лист и проверка"));
  problemId_->setObjectName(QStringLiteral("dxfProblemId"));
  sheetWidth_->setObjectName(QStringLiteral("dxfSheetWidth"));
  sheetHeight_->setObjectName(QStringLiteral("dxfSheetHeight"));
  curveTolerance_->setValue(0.05);
  resultLabel_->setWordWrap(true);
  resultLabel_->setObjectName(QStringLiteral("dxfImportResult"));
  auto * taskForm = new QFormLayout(taskPage);
  taskForm->addRow(tr("Идентификатор задачи:"), problemId_);
  taskForm->addRow(tr("Ширина листа:"), sheetWidth_);
  taskForm->addRow(tr("Высота листа:"), sheetHeight_);
  taskForm->addRow(tr("Отступ от края:"), sheetMargin_);
  taskForm->addRow(tr("Зазор деталей:"), partSpacing_);
  taskForm->addRow(tr("Ширина реза:"), kerf_);
  taskForm->addRow(tr("Допуск кривых:"), curveTolerance_);
  taskForm->addRow(tr("Результат:"), resultLabel_);
  setPage(4, taskPage);
  setStartId(0);
}

/// Запускает новый анализ с подтверждёнными на первых страницах параметрами.
void PolygonDxfImportWizard::reanalyze()
{
  if (!actions_.inspect || filePath_.isEmpty())
    return;
  PolygonImportInspectionRequest request;
  request.filePath = filePath_.toStdString();
  request.unit = selectedUnit();
  request.joinToleranceMm = joinTolerance_->value();
  for (int row = 0; row < joinsList_->count(); ++row)
  {
    const QListWidgetItem * item = joinsList_->item(row);
    if (item->checkState() == Qt::Checked)
      request.acceptedJoinProposals.push_back(item->data(IDENTIFIER_ROLE).toULongLong());
  }
  appliedJoins_ = request.acceptedJoinProposals;
  inspectionPopulated_ = false;
  actions_.inspect(request);
}

/// Отображает слои, предложения и начальные назначения без предметной логики Qt.
void PolygonDxfImportWizard::populateInspection(const PolygonImportInspection & inspection)
{
  const int detected = unitCombo_->findData(static_cast<int>(inspection.detectedUnit));
  if (detected >= 0 && !inspection.unitSelectionRequired)
    unitCombo_->setCurrentIndex(detected);
  layersList_->clear();
  for (const auto & layer : inspection.layers)
  {
    auto * item = new QListWidgetItem(tr("%1 — поддержано: %2, неподдерживаемо: %3")
                                        .arg(QString::fromStdString(layer.name))
                                        .arg(layer.supportedEntities)
                                        .arg(layer.unsupportedEntities),
                                      layersList_);
    item->setData(IDENTIFIER_ROLE, QString::fromStdString(layer.name));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(layer.selected ? Qt::Checked : Qt::Unchecked);
  }
  joinsList_->clear();
  for (const auto & proposal : inspection.joinProposals)
  {
    auto * item = new QListWidgetItem(
      tr("Цепочки %1 и %2, разрыв %3 мм").arg(proposal.firstPath).arg(proposal.secondPath).arg(proposal.distanceMm, 0, 'f', 4),
      joinsList_);
    item->setData(IDENTIFIER_ROLE, QVariant::fromValue<qulonglong>(proposal.id));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(
      std::find(appliedJoins_.begin(), appliedJoins_.end(), proposal.id) != appliedJoins_.end() ? Qt::Checked : Qt::Unchecked);
  }
  assignmentsTable_->setRowCount(static_cast<int>(inspection.paths.size()));
  for (int row = 0; row < assignmentsTable_->rowCount(); ++row)
  {
    const auto & path = inspection.paths[static_cast<std::size_t>(row)];
    auto * id = new QTableWidgetItem(QString::number(path.id));
    id->setData(IDENTIFIER_ROLE, QVariant::fromValue<qulonglong>(path.id));
    assignmentsTable_->setItem(row, 0, id);
    assignmentsTable_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(path.layer)));
    auto * role = new QComboBox(assignmentsTable_);
    role->addItem(tr("Деталь"), 0);
    role->addItem(tr("Отверстие"), 1);
    role->addItem(tr("Не импортировать"), 2);
    role->setCurrentIndex(path.suggestedOuterPath == 0 ? 0 : 1);
    assignmentsTable_->setCellWidget(row, 2, role);
    auto * parent = new QComboBox(assignmentsTable_);
    parent->addItem(tr("—"), QVariant::fromValue<qulonglong>(0));
    for (const auto & candidate : inspection.paths)
    {
      if (candidate.closed && candidate.suggestedOuterPath == 0)
        parent->addItem(QString::number(candidate.id), QVariant::fromValue<qulonglong>(candidate.id));
    }
    const int parentIndex = parent->findData(QVariant::fromValue<qulonglong>(path.suggestedOuterPath));
    if (parentIndex >= 0)
      parent->setCurrentIndex(parentIndex);
    assignmentsTable_->setCellWidget(row, 3, parent);
  }
  preview_->setPaths(inspection.paths);
  double width = 0.0;
  double height = 0.0;
  for (const auto & path : inspection.paths)
  {
    width = std::max(width, path.maxX - path.minX);
    height = std::max(height, path.maxY - path.minY);
  }
  sheetWidth_->setValue(std::max(1.0, std::ceil(width)));
  sheetHeight_->setValue(std::max(1.0, std::ceil(height)));
  resultLabel_->setText(tr("Проверьте параметры и нажмите «Построить документ»."));
}

/// Создаёт редактируемые строки только для цепочек, назначенных деталями.
void PolygonDxfImportWizard::rebuildParts()
{
  std::map<std::uint64_t, std::tuple<QString, QString, QString>> previous;
  for (int row = 0; row < partsTable_->rowCount(); ++row)
  {
    const std::uint64_t id = partsTable_->item(row, 0)->data(IDENTIFIER_ROLE).toULongLong();
    previous[id] = {qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 1))->text(),
                    qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 2))->text(),
                    qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 3))->text()};
  }
  std::vector<std::uint64_t> outers;
  for (int row = 0; row < assignmentsTable_->rowCount(); ++row)
  {
    const auto * role = qobject_cast<QComboBox *>(assignmentsTable_->cellWidget(row, 2));
    if (role && role->currentData().toInt() == 0)
      outers.push_back(assignmentsTable_->item(row, 0)->data(IDENTIFIER_ROLE).toULongLong());
  }
  partsTable_->setRowCount(static_cast<int>(outers.size()));
  for (int row = 0; row < partsTable_->rowCount(); ++row)
  {
    const std::uint64_t id = outers[static_cast<std::size_t>(row)];
    auto * item = new QTableWidgetItem(QString::number(id));
    item->setData(IDENTIFIER_ROLE, QVariant::fromValue<qulonglong>(id));
    partsTable_->setItem(row, 0, item);
    auto * partId = new QLineEdit(partsTable_);
    auto * quantity = new QLineEdit(partsTable_);
    auto * rotations = new QLineEdit(partsTable_);
    const auto found = previous.find(id);
    partId->setText(found == previous.end() ? tr("деталь-%1").arg(row + 1) : std::get<0>(found->second));
    quantity->setText(found == previous.end() ? QStringLiteral("1") : std::get<1>(found->second));
    rotations->setText(found == previous.end() ? QStringLiteral("0") : std::get<2>(found->second));
    partsTable_->setCellWidget(row, 1, partId);
    partsTable_->setCellWidget(row, 2, quantity);
    partsTable_->setCellWidget(row, 3, rotations);
  }
}

/// Переносит выбранные элементы управления в нейтральные прикладные структуры.
PolygonImportConfiguration PolygonDxfImportWizard::configuration() const
{
  PolygonImportConfiguration result;
  result.unit = selectedUnit();
  result.joinToleranceMm = joinTolerance_->value();
  for (int row = 0; row < layersList_->count(); ++row)
  {
    const QListWidgetItem * item = layersList_->item(row);
    if (item->checkState() == Qt::Checked)
      result.selectedLayers.push_back(item->data(IDENTIFIER_ROLE).toString().toStdString());
  }
  for (int row = 0; row < joinsList_->count(); ++row)
  {
    const QListWidgetItem * item = joinsList_->item(row);
    if (item->checkState() == Qt::Checked)
      result.acceptedJoinProposals.push_back(item->data(IDENTIFIER_ROLE).toULongLong());
  }
  result.ignoreUnsupported = ignoreUnsupported_->isChecked();
  result.groupDuplicateParts = groupDuplicates_->isChecked();
  for (int row = 0; row < partsTable_->rowCount(); ++row)
  {
    PolygonImportPart part;
    part.outerPath = partsTable_->item(row, 0)->data(IDENTIFIER_ROLE).toULongLong();
    part.partId = qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 1))->text().trimmed().toStdString();
    bool ok = false;
    const qulonglong quantity = qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 2))->text().toULongLong(&ok);
    part.quantity = ok && quantity <= std::numeric_limits<std::uint32_t>::max() ? static_cast<std::uint32_t>(quantity) : 0;
    part.allowedRotations = rotationsFromText(qobject_cast<QLineEdit *>(partsTable_->cellWidget(row, 3))->text());
    for (int assignment = 0; assignment < assignmentsTable_->rowCount(); ++assignment)
    {
      const auto * role = qobject_cast<QComboBox *>(assignmentsTable_->cellWidget(assignment, 2));
      const auto * parent = qobject_cast<QComboBox *>(assignmentsTable_->cellWidget(assignment, 3));
      if (role && parent && role->currentData().toInt() == 1 && parent->currentData().toULongLong() == part.outerPath)
        part.holes.push_back(assignmentsTable_->item(assignment, 0)->data(IDENTIFIER_ROLE).toULongLong());
    }
    result.parts.push_back(std::move(part));
  }
  result.problemId = problemId_->text().trimmed().toStdString();
  result.sheetWidth = sheetWidth_->value();
  result.sheetHeight = sheetHeight_->value();
  result.sheetMargin = sheetMargin_->value();
  result.partSpacing = partSpacing_->value();
  result.kerf = kerf_->value();
  result.curveTolerance = curveTolerance_->value();
  return result;
}

/// Читает перечисление из машинных данных выбранного элемента списка.
PolygonImportUnit PolygonDxfImportWizard::selectedUnit() const
{
  return static_cast<PolygonImportUnit>(unitCombo_->currentData().toInt());
}

/// Различает запрос построения и окончательное принятие уже проверенного результата.
void PolygonDxfImportWizard::accept()
{
  if (snapshot_.state != PolygonImportState::Completed)
  {
    // Состав деталей должен соответствовать текущим назначениям даже при
    // программном переходе между страницами без показа окна.
    rebuildParts();
    if (actions_.build)
      actions_.build(configuration());
    return;
  }
  if (actions_.accept)
    actions_.accept(!snapshot_.resultValid);
  QWizard::accept();
}

/// Перед закрытием отменяет работу и освобождает временный сеанс.
void PolygonDxfImportWizard::reject()
{
  if ((snapshot_.state == PolygonImportState::Inspecting || snapshot_.state == PolygonImportState::Building) && actions_.cancel)
    actions_.cancel();
  if (actions_.close)
    actions_.close();
  QWizard::reject();
}
