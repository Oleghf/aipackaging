#include <algorithm>
#include <cstdint>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTreeWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <utility>

#include <polygoneditorpanel.h>

using namespace aipackaging::editor;

namespace
{
constexpr int ENTITY_KIND_ROLE = Qt::UserRole + 1;
constexpr int ENTITY_ID_ROLE = Qt::UserRole + 2;
constexpr int PART_ID_ROLE = Qt::UserRole + 3;
constexpr int PATH_ID_ROLE = Qt::UserRole + 4;
enum EntityKind : std::uint8_t
{
  PartKind = 1,
  PathKind = 2,
  PointKind = 3,
  SegmentKind = 4
};

/// Создаёт поле конечного миллиметрового значения с общим диапазоном редактора.
QDoubleSpinBox * coordinateBox(QWidget * parent)
{
  auto * box = new QDoubleSpinBox(parent);
  box->setDecimals(6);
  box->setRange(-1.0e9, 1.0e9);
  box->setSuffix(QObject::tr(" мм"));
  return box;
}

/// Находит часть документа по устойчивому идентификатору.
const EditablePart * findPart(const EditablePolygonDocument & document, EntityId id)
{
  const auto found =
    std::find_if(document.parts.begin(), document.parts.end(), [id](const EditablePart & part) { return part.id == id; });
  return found == document.parts.end() ? nullptr : &*found;
}

/// Находит цепочку документа по устойчивому идентификатору.
const EditablePath * findPath(const EditablePolygonDocument & document, EntityId id)
{
  for (const EditablePart & part : document.parts)
  {
    if (part.outer && part.outer->id == id)
      return &*part.outer;
    const auto hole =
      std::find_if(part.holes.begin(), part.holes.end(), [id](const EditablePath & path) { return path.id == id; });
    if (hole != part.holes.end())
      return &*hole;
  }
  return nullptr;
}

/// Находит точку среди вершин и параметров кривых.
const EditablePoint * findPoint(const EditablePolygonDocument & document, EntityId id)
{
  auto inspect = [id](const EditablePath & path) -> const EditablePoint *
  {
    for (const EditablePoint & point : path.vertices)
      if (point.id == id)
        return &point;
    for (const EditableSegment & segment : path.segments)
    {
      if (segment.kind == EditableSegmentKind::Arc && segment.center.id == id)
        return &segment.center;
      if (segment.kind == EditableSegmentKind::CubicBezier)
      {
        if (segment.control1.id == id)
          return &segment.control1;
        if (segment.control2.id == id)
          return &segment.control2;
      }
    }
    return nullptr;
  };
  for (const EditablePart & part : document.parts)
  {
    if (part.outer)
      if (const EditablePoint * point = inspect(*part.outer))
        return point;
    for (const EditablePath & hole : part.holes)
      if (const EditablePoint * point = inspect(hole))
        return point;
  }
  return nullptr;
}

/// Находит сегмент документа по устойчивому идентификатору.
const EditableSegment * findSegment(const EditablePolygonDocument & document, EntityId id)
{
  auto inspect = [id](const EditablePath & path) -> const EditableSegment *
  {
    const auto found = std::find_if(path.segments.begin(), path.segments.end(),
                                    [id](const EditableSegment & segment) { return segment.id == id; });
    return found == path.segments.end() ? nullptr : &*found;
  };
  for (const EditablePart & part : document.parts)
  {
    if (part.outer)
      if (const EditableSegment * segment = inspect(*part.outer))
        return segment;
    for (const EditablePath & hole : part.holes)
      if (const EditableSegment * segment = inspect(hole))
        return segment;
  }
  return nullptr;
}
} // namespace

/// Создаёт пустые элементы управления и делегирует их компоновку отдельному построителю.
PolygonEditorPanel::PolygonEditorPanel(QWidget * parent)
  : QWidget(parent)
  , tree_(new QTreeWidget(this))
  , problemId_(new QLineEdit(this))
  , sheetWidth_(coordinateBox(this))
  , sheetHeight_(coordinateBox(this))
  , sheetMargin_(coordinateBox(this))
  , spacing_(coordinateBox(this))
  , kerf_(coordinateBox(this))
  , tolerance_(coordinateBox(this))
  , partId_(new QLineEdit(this))
  , quantity_(new QSpinBox(this))
  , rotations_(new QLineEdit(this))
  , segmentKind_(new QComboBox(this))
  , clockwise_(new QCheckBox(tr("По часовой стрелке"), this))
  , pointX_(coordinateBox(this))
  , pointY_(coordinateBox(this))
  , auxiliary1X_(coordinateBox(this))
  , auxiliary1Y_(coordinateBox(this))
  , auxiliary2X_(coordinateBox(this))
  , auxiliary2Y_(coordinateBox(this))
  , diagnostics_(new QListWidget(this))
  , commandArea_(new QWidget(this))
{
  buildUi();
}

/// Собирает прокручиваемую панель и связывает каждую кнопку с одной предметной командой.
void PolygonEditorPanel::buildUi()
{
  setObjectName(QStringLiteral("polygonEditorPanel"));
  setMinimumWidth(330);
  problemId_->setObjectName(QStringLiteral("editorProblemId"));
  sheetWidth_->setObjectName(QStringLiteral("editorSheetWidth"));
  sheetHeight_->setObjectName(QStringLiteral("editorSheetHeight"));
  sheetMargin_->setObjectName(QStringLiteral("editorSheetMargin"));
  spacing_->setObjectName(QStringLiteral("editorPartSpacing"));
  kerf_->setObjectName(QStringLiteral("editorKerf"));
  tolerance_->setObjectName(QStringLiteral("editorCurveTolerance"));
  partId_->setObjectName(QStringLiteral("editorPartId"));
  quantity_->setObjectName(QStringLiteral("editorQuantity"));
  rotations_->setObjectName(QStringLiteral("editorRotations"));
  segmentKind_->setObjectName(QStringLiteral("editorSegmentKind"));
  clockwise_->setObjectName(QStringLiteral("editorArcClockwise"));
  pointX_->setObjectName(QStringLiteral("editorPointX"));
  pointY_->setObjectName(QStringLiteral("editorPointY"));
  tree_->setObjectName(QStringLiteral("editorEntityTree"));
  tree_->setHeaderLabel(tr("Структура документа"));
  tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  tree_->header()->setStretchLastSection(true);
  diagnostics_->setObjectName(QStringLiteral("editorDiagnostics"));
  diagnostics_->setMinimumHeight(90);
  quantity_->setRange(0, 1000000);
  rotations_->setPlaceholderText(tr("0, 90, 180, 270"));
  segmentKind_->addItems({tr("Отрезок"), tr("Дуга"), tr("Кубическая Bézier")});

  auto * documentGroup = new QGroupBox(tr("Документ и лист"), commandArea_);
  auto * documentForm = new QFormLayout(documentGroup);
  documentForm->addRow(tr("Идентификатор"), problemId_);
  documentForm->addRow(tr("Ширина"), sheetWidth_);
  documentForm->addRow(tr("Высота"), sheetHeight_);
  documentForm->addRow(tr("Отступ листа"), sheetMargin_);
  documentForm->addRow(tr("Зазор деталей"), spacing_);
  documentForm->addRow(tr("Ширина реза"), kerf_);
  documentForm->addRow(tr("Допуск кривых"), tolerance_);
  auto * applyDocument = new QPushButton(tr("Применить свойства"), documentGroup);
  applyDocument->setObjectName(QStringLiteral("applyDocumentPropertiesButton"));
  documentForm->addRow(applyDocument);

  auto * partGroup = new QGroupBox(tr("Тип детали"), commandArea_);
  auto * partForm = new QFormLayout(partGroup);
  selectionHint_ = new QLabel(tr("Выберите одну сущность для изменения свойств"), partGroup);
  selectionHint_->setWordWrap(true);
  partForm->addRow(selectionHint_);
  partForm->addRow(tr("Идентификатор"), partId_);
  partForm->addRow(tr("Количество"), quantity_);
  partForm->addRow(tr("Повороты"), rotations_);
  auto * partButtons = new QHBoxLayout();
  auto * addPart = new QPushButton(tr("Добавить"), partGroup);
  auto * duplicatePart = new QPushButton(tr("Дублировать"), partGroup);
  auto * deletePart = new QPushButton(tr("Удалить"), partGroup);
  auto * applyPart = new QPushButton(tr("Применить"), partGroup);
  addPart->setObjectName(QStringLiteral("addPartButton"));
  duplicatePart->setObjectName(QStringLiteral("duplicatePartButton"));
  deletePart->setObjectName(QStringLiteral("deletePartButton"));
  applyPart->setObjectName(QStringLiteral("applyPartButton"));
  partButtons->addWidget(addPart);
  partButtons->addWidget(duplicatePart);
  partButtons->addWidget(deletePart);
  partForm->addRow(partButtons);
  partForm->addRow(applyPart);

  auto * geometryGroup = new QGroupBox(tr("Контур и сегмент"), commandArea_);
  geometryGroup->setObjectName(QStringLiteral("editorGeometryGroup"));
  auto * geometryForm = new QFormLayout(geometryGroup);
  geometryForm->addRow(tr("Вид сегмента"), segmentKind_);
  geometryForm->addRow(tr("Направление дуги"), clockwise_);
  geometryForm->addRow(tr("X вершины"), pointX_);
  geometryForm->addRow(tr("Y вершины"), pointY_);
  geometryForm->addRow(tr("X центра / C1"), auxiliary1X_);
  geometryForm->addRow(tr("Y центра / C1"), auxiliary1Y_);
  geometryForm->addRow(tr("X C2"), auxiliary2X_);
  geometryForm->addRow(tr("Y C2"), auxiliary2Y_);
  auto * createPathRow = new QHBoxLayout();
  auto * createOuter = new QPushButton(tr("Внешний контур"), geometryGroup);
  auto * createHole = new QPushButton(tr("Отверстие"), geometryGroup);
  createOuter->setObjectName(QStringLiteral("createOuterPathButton"));
  createHole->setObjectName(QStringLiteral("createHolePathButton"));
  createPathRow->addWidget(createOuter);
  createPathRow->addWidget(createHole);
  geometryForm->addRow(createPathRow);
  auto * segmentRow = new QHBoxLayout();
  auto * appendSegment = new QPushButton(tr("Добавить сегмент"), geometryGroup);
  auto * updateSegment = new QPushButton(tr("Изменить"), geometryGroup);
  auto * deleteSegment = new QPushButton(tr("Удалить"), geometryGroup);
  appendSegment->setObjectName(QStringLiteral("appendSegmentButton"));
  updateSegment->setObjectName(QStringLiteral("updateSegmentButton"));
  deleteSegment->setObjectName(QStringLiteral("deleteSegmentButton"));
  segmentRow->addWidget(appendSegment);
  segmentRow->addWidget(updateSegment);
  segmentRow->addWidget(deleteSegment);
  geometryForm->addRow(segmentRow);
  auto * pathRow = new QHBoxLayout();
  auto * closePath = new QPushButton(tr("Замкнуть"), geometryGroup);
  auto * openPath = new QPushButton(tr("Разомкнуть"), geometryGroup);
  auto * deletePath = new QPushButton(tr("Удалить контур"), geometryGroup);
  pathRow->addWidget(closePath);
  pathRow->addWidget(openPath);
  pathRow->addWidget(deletePath);
  geometryForm->addRow(pathRow);
  auto * movePoint = new QPushButton(tr("Переместить выбранную точку"), geometryGroup);
  movePoint->setObjectName(QStringLiteral("moveEditorPointButton"));
  geometryForm->addRow(movePoint);

  auto * commandLayout = new QVBoxLayout(commandArea_);
  commandLayout->setContentsMargins(0, 0, 0, 0);
  commandLayout->addWidget(documentGroup);
  commandLayout->addWidget(partGroup);
  commandLayout->addWidget(geometryGroup);
  commandLayout->addStretch(1);
  auto * scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setWidget(commandArea_);
  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  auto * sections = new QSplitter(Qt::Vertical, this);
  sections->setObjectName(QStringLiteral("editorSectionsSplitter"));
  tree_->setMinimumHeight(65);
  scroll->setMinimumHeight(180);
  sections->addWidget(tree_);
  sections->addWidget(scroll);
  sections->setChildrenCollapsible(false);
  sections->setStretchFactor(0, 1);
  sections->setStretchFactor(1, 3);
  const QByteArray saved = QSettings().value(QStringLiteral("ui/editorSectionsSplitter")).toByteArray();
  if (saved.isEmpty() || !sections->restoreState(saved))
    sections->setSizes({120, 360});
  connect(sections, &QSplitter::splitterMoved, this,
          [sections]() { QSettings().setValue(QStringLiteral("ui/editorSectionsSplitter"), sections->saveState()); });
  layout->addWidget(sections, 1);
  // Совместимый объект остаётся доступен тестам, но не дублирует постоянно видимую панель проблем.
  diagnostics_->hide();
  problemsButton_ = new QPushButton(tr("Проблемы документа: 0"), this);
  problemsButton_->setObjectName(QStringLiteral("editorProblemsButton"));
  connect(problemsButton_, &QPushButton::clicked, this, &PolygonEditorPanel::problemsRequested);
  layout->addWidget(problemsButton_);

  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &PolygonEditorPanel::presentSelection);
  connect(applyDocument, &QPushButton::clicked, this,
          [this]()
          {
            EditableManufacturing manufacturing{sheetMargin_->value(), spacing_->value(), kerf_->value(), tolerance_->value()};
            submit("Свойства документа",
                   {SetProblemIdCommand{problemId_->text().toStdString()},
                    SetSheetCommand{sheetWidth_->value(), sheetHeight_->value()}, SetManufacturingCommand{manufacturing}});
          });
  connect(addPart, &QPushButton::clicked, this,
          [this]()
          {
            int suffix = static_cast<int>(snapshot_.editableDocument ? snapshot_.editableDocument->parts.size() + 1 : 1);
            std::string name;
            do
            {
              name = "detail-" + std::to_string(suffix++);
            } while (snapshot_.editableDocument &&
                     std::any_of(snapshot_.editableDocument->parts.begin(), snapshot_.editableDocument->parts.end(),
                                 [&name](const EditablePart & part) { return part.partId == name; }));
            submit("Добавление детали", {AddPartCommand{name, 1, {0}}});
          });
  connect(applyPart, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId part = selectedPart();
            if (part)
              submit("Свойства детали", {SetPartPropertiesCommand{part, partId_->text().toStdString(),
                                                                  static_cast<std::uint32_t>(quantity_->value()), rotations()}});
          });
  connect(duplicatePart, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId part = selectedPart();
            if (part)
            {
              const std::string base = partId_->text().toStdString() + "-copy";
              std::string name = base;
              int suffix = 2;
              while (snapshot_.editableDocument &&
                     std::any_of(snapshot_.editableDocument->parts.begin(), snapshot_.editableDocument->parts.end(),
                                 [&name](const EditablePart & candidate) { return candidate.partId == name; }))
                name = base + "-" + std::to_string(suffix++);
              submit("Дублирование детали", {DuplicatePartCommand{part, std::move(name)}});
            }
          });
  connect(deletePart, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId part = selectedPart();
            if (part && QMessageBox::question(this, tr("Удаление детали"),
                                              tr("Удалить выбранный тип детали и всю его геометрию?")) == QMessageBox::Yes)
              submit("Удаление детали", {DeletePartCommand{part}});
          });
  connect(createOuter, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId part = selectedPart();
            if (part)
              submit("Создание внешнего контура", {CreatePathCommand{part, false, {pointX_->value(), pointY_->value()}}});
          });
  connect(createHole, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId part = selectedPart();
            if (part)
              submit("Создание отверстия", {CreatePathCommand{part, true, {pointX_->value(), pointY_->value()}}});
          });
  connect(appendSegment, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId path = selectedPath();
            if (path)
              submit("Добавление сегмента", {AppendSegmentCommand{path, {pointX_->value(), pointY_->value()}, segmentValue()}});
          });
  connect(updateSegment, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId segment = selectedEntity(SegmentKind);
            if (segment)
              submit("Изменение сегмента", {UpdateSegmentCommand{segment, segmentValue()}});
          });
  connect(deleteSegment, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId segment = selectedEntity(SegmentKind);
            if (segment)
              submit("Удаление сегмента", {DeleteSegmentCommand{segment}});
          });
  connect(closePath, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId path = selectedPath();
            if (path)
              submit("Замыкание контура", {ClosePathCommand{path, segmentValue()}});
          });
  connect(openPath, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId path = selectedPath();
            if (path)
              submit("Размыкание контура", {OpenPathCommand{path}});
          });
  connect(deletePath, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId path = selectedPath();
            if (path && QMessageBox::question(this, tr("Удаление контура"), tr("Удалить выбранный контур?")) == QMessageBox::Yes)
              submit("Удаление контура", {DeletePathCommand{path}});
          });
  connect(movePoint, &QPushButton::clicked, this,
          [this]()
          {
            const EntityId point = selectedEntity(PointKind);
            if (point)
              submit("Перемещение точки", {MovePointCommand{point, {pointX_->value(), pointY_->value()}}});
          });
}

/// Копирует снимок, обновляет формы документа и перестраивает дерево устойчивых сущностей.
void PolygonEditorPanel::present(const PolygonWorkspaceSnapshot & snapshot)
{
  const bool documentChanged = treeDocument_ != snapshot.editableDocument;
  std::vector<std::uint64_t> selected;
  for (const QTreeWidgetItem * item : tree_->selectedItems())
    selected.push_back(item->data(0, ENTITY_ID_ROLE).toULongLong());
  if (snapshot.documentIdentity != snapshot_.documentIdentity ||
      snapshot.document.sourceIdentifier != snapshot_.document.sourceIdentifier)
    selected.clear();
  snapshot_ = snapshot;
  updating_ = true;
  commandArea_->setEnabled(snapshot.canEdit);
  if (snapshot.editableDocument)
  {
    problemId_->setText(QString::fromStdString(snapshot.editableDocument->problemId));
    sheetWidth_->setValue(snapshot.editableDocument->sheet.width);
    sheetHeight_->setValue(snapshot.editableDocument->sheet.height);
    sheetMargin_->setValue(snapshot.editableDocument->manufacturing.sheetMargin);
    spacing_->setValue(snapshot.editableDocument->manufacturing.partSpacing);
    kerf_->setValue(snapshot.editableDocument->manufacturing.kerf);
    tolerance_->setValue(snapshot.editableDocument->manufacturing.curveTolerance);
  }
  if (documentChanged)
  {
    treeDocument_ = snapshot.editableDocument;
    rebuildTree();
  }
  selectEntities(selected);
  diagnostics_->clear();
  for (const DocumentDiagnostic & diagnostic : snapshot.documentDiagnostics)
    diagnostics_->addItem(QString::fromStdString(diagnostic.message));
  problemsButton_->setText(tr("Проблемы документа: %1").arg(snapshot.documentDiagnostics.size()));
  updating_ = false;
  presentSelection();
}

/// Сопоставляет идентификаторы новым элементам дерева после каждой публикации снимка.
void PolygonEditorPanel::selectEntities(const std::vector<std::uint64_t> & entityIds)
{
  const bool previous = updating_;
  updating_ = true;
  tree_->clearSelection();
  QTreeWidgetItem * current = nullptr;
  const auto items = tree_->findItems(QString(), Qt::MatchContains | Qt::MatchRecursive);
  for (QTreeWidgetItem * item : items)
    if (std::find(entityIds.begin(), entityIds.end(), item->data(0, ENTITY_ID_ROLE).toULongLong()) != entityIds.end())
    {
      item->setSelected(true);
      if (!current)
        current = item;
    }
  tree_->setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
  updating_ = previous;
  if (!updating_)
    presentSelection();
}

/// Создаёт иерархию часть–контур–вершина/сегмент, записывая принадлежность в роли Qt.
void PolygonEditorPanel::rebuildTree()
{
  tree_->clear();
  if (!snapshot_.editableDocument)
    return;
  for (const EditablePart & part : snapshot_.editableDocument->parts)
  {
    auto * partItem = new QTreeWidgetItem(tree_, {QString::fromStdString(part.partId)});
    partItem->setData(0, ENTITY_KIND_ROLE, PartKind);
    partItem->setData(0, ENTITY_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
    partItem->setData(0, PART_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
    auto addPath = [&](const EditablePath & path, QString title)
    {
      auto * pathItem = new QTreeWidgetItem(partItem, {std::move(title)});
      pathItem->setData(0, ENTITY_KIND_ROLE, PathKind);
      pathItem->setData(0, ENTITY_ID_ROLE, QVariant::fromValue<qulonglong>(path.id.value));
      pathItem->setData(0, PART_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
      pathItem->setData(0, PATH_ID_ROLE, QVariant::fromValue<qulonglong>(path.id.value));
      for (std::size_t index = 0; index < path.vertices.size(); ++index)
      {
        auto * point = new QTreeWidgetItem(pathItem, {tr("Вершина %1").arg(index + 1)});
        point->setData(0, ENTITY_KIND_ROLE, PointKind);
        point->setData(0, ENTITY_ID_ROLE, QVariant::fromValue<qulonglong>(path.vertices[index].id.value));
        point->setData(0, PART_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
        point->setData(0, PATH_ID_ROLE, QVariant::fromValue<qulonglong>(path.id.value));
      }
      for (std::size_t index = 0; index < path.segments.size(); ++index)
      {
        const EditableSegment & segment = path.segments[index];
        auto * item = new QTreeWidgetItem(pathItem, {tr("Сегмент %1").arg(index + 1)});
        item->setData(0, ENTITY_KIND_ROLE, SegmentKind);
        item->setData(0, ENTITY_ID_ROLE, QVariant::fromValue<qulonglong>(segment.id.value));
        item->setData(0, PART_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
        item->setData(0, PATH_ID_ROLE, QVariant::fromValue<qulonglong>(path.id.value));
        auto addControl = [&](const EditablePoint & control, const QString & text)
        {
          auto * child = new QTreeWidgetItem(item, {text});
          child->setData(0, ENTITY_KIND_ROLE, PointKind);
          child->setData(0, ENTITY_ID_ROLE, QVariant::fromValue<qulonglong>(control.id.value));
          child->setData(0, PART_ID_ROLE, QVariant::fromValue<qulonglong>(part.id.value));
          child->setData(0, PATH_ID_ROLE, QVariant::fromValue<qulonglong>(path.id.value));
        };
        if (segment.kind == EditableSegmentKind::Arc)
          addControl(segment.center, tr("Центр дуги"));
        if (segment.kind == EditableSegmentKind::CubicBezier)
        {
          addControl(segment.control1, tr("Контрольная точка 1"));
          addControl(segment.control2, tr("Контрольная точка 2"));
        }
      }
    };
    if (part.outer)
      addPath(*part.outer, tr("Внешний контур%1").arg(part.outer->closed ? QString() : tr(" — открыт")));
    for (std::size_t index = 0; index < part.holes.size(); ++index)
      addPath(part.holes[index],
              tr("Отверстие %1%2").arg(index + 1).arg(part.holes[index].closed ? QString() : tr(" — открыто")));
  }
  tree_->expandToDepth(1);
}

/// Находит выбранную сущность в снимке и заполняет относящиеся к ней поля.
void PolygonEditorPanel::presentSelection()
{
  if (updating_)
    return;
  const bool single = snapshot_.editableDocument && tree_->currentItem() && tree_->selectedItems().size() == 1;
  selectionHint_->setVisible(!single);
  for (QWidget * field : {static_cast<QWidget *>(partId_), static_cast<QWidget *>(quantity_), static_cast<QWidget *>(rotations_)})
    field->setEnabled(single && snapshot_.canEdit);
  for (const char * name : {"duplicatePartButton", "deletePartButton", "applyPartButton", "editorGeometryGroup"})
    if (auto * control = findChild<QWidget *>(QString::fromLatin1(name)))
      control->setEnabled(single && snapshot_.canEdit);
  if (!single)
  {
    partId_->clear();
    quantity_->setValue(1);
    rotations_->setText(QStringLiteral("0"));
    for (auto * coordinate : {pointX_, pointY_, auxiliary1X_, auxiliary1Y_, auxiliary2X_, auxiliary2Y_})
      coordinate->setValue(0);
    if (tree_->selectedItems().isEmpty())
    {
      emit entitySelected(0);
      emit entitiesSelected({});
    }
    else
    {
      std::vector<std::uint64_t> ids;
      for (const auto * item : tree_->selectedItems())
        ids.push_back(item->data(0, ENTITY_ID_ROLE).toULongLong());
      emit entitiesSelected(ids);
    }
    return;
  }
  const EntityId partId = selectedPart();
  for (auto * coordinate : {pointX_, pointY_, auxiliary1X_, auxiliary1Y_, auxiliary2X_, auxiliary2Y_})
    coordinate->setValue(0);
  clockwise_->setChecked(false);
  if (const EditablePart * part = findPart(*snapshot_.editableDocument, partId))
  {
    partId_->setText(QString::fromStdString(part->partId));
    quantity_->setValue(static_cast<int>(part->quantity));
    QStringList values;
    for (int rotation : part->allowedRotations)
      values.push_back(QString::number(rotation));
    rotations_->setText(values.join(QStringLiteral(", ")));
  }
  const EntityId selected{tree_->currentItem()->data(0, ENTITY_ID_ROLE).toULongLong()};
  emit entitySelected(selected.value);
  std::vector<std::uint64_t> selectedIds;
  for (const QTreeWidgetItem * item : tree_->selectedItems())
    selectedIds.push_back(item->data(0, ENTITY_ID_ROLE).toULongLong());
  emit entitiesSelected(selectedIds);
  if (const EditablePoint * point = findPoint(*snapshot_.editableDocument, selected))
  {
    pointX_->setValue(point->x);
    pointY_->setValue(point->y);
  }
  if (const EditableSegment * segment = findSegment(*snapshot_.editableDocument, selected))
  {
    segmentKind_->setCurrentIndex(static_cast<int>(segment->kind));
    if (segment->kind == EditableSegmentKind::Arc)
    {
      auxiliary1X_->setValue(segment->center.x);
      auxiliary1Y_->setValue(segment->center.y);
      clockwise_->setChecked(segment->clockwise);
    }
    if (segment->kind == EditableSegmentKind::CubicBezier)
    {
      auxiliary1X_->setValue(segment->control1.x);
      auxiliary1Y_->setValue(segment->control1.y);
      auxiliary2X_->setValue(segment->control2.x);
      auxiliary2Y_->setValue(segment->control2.y);
    }
  }
}

/// Проверяет вид текущего элемента перед возвратом его идентификатора.
EntityId PolygonEditorPanel::selectedEntity(int kind) const
{
  const QTreeWidgetItem * item = tree_->currentItem();
  return item && item->data(0, ENTITY_KIND_ROLE).toInt() == kind ? EntityId{item->data(0, ENTITY_ID_ROLE).toULongLong()}
                                                                 : EntityId{};
}

/// Читает принадлежность к детали из роли любого уровня дерева.
EntityId PolygonEditorPanel::selectedPart() const
{
  const QTreeWidgetItem * item = tree_->currentItem();
  return item ? EntityId{item->data(0, PART_ID_ROLE).toULongLong()} : EntityId{};
}

/// Возвращает сам выбранный контур либо контур выбранной дочерней сущности.
EntityId PolygonEditorPanel::selectedPath() const
{
  const QTreeWidgetItem * item = tree_->currentItem();
  return item ? EntityId{item->data(0, PATH_ID_ROLE).toULongLong()} : EntityId{};
}

/// Игнорирует пустые элементы и сохраняет введённый порядок углов.
std::vector<int> PolygonEditorPanel::rotations() const
{
  std::vector<int> result;
  for (const QString & item : rotations_->text().split(',', Qt::SkipEmptyParts))
  {
    bool valid = false;
    const int value = item.trimmed().toInt(&valid);
    if (valid)
      result.push_back(value);
  }
  return result;
}

/// Интерпретирует первые вспомогательные координаты как центр дуги либо первую контрольную точку.
EditorSegmentValue PolygonEditorPanel::segmentValue() const
{
  EditorSegmentValue result;
  result.kind = static_cast<EditableSegmentKind>(segmentKind_->currentIndex());
  result.center = {auxiliary1X_->value(), auxiliary1Y_->value()};
  result.control1 = result.center;
  result.control2 = {auxiliary2X_->value(), auxiliary2Y_->value()};
  result.clockwise = clockwise_->isChecked();
  return result;
}

/// Создаёт обычную транзакцию без жеста и не обходит опубликованную доступность редактирования.
void PolygonEditorPanel::submit(std::string label, std::vector<EditorCommand> commands)
{
  if (snapshot_.canEdit)
    emit editRequested(EditorCommandBatch{std::move(label), std::move(commands), std::nullopt});
}
