#include <algorithm>
#include <cmath>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolBar>

#include <polygoneditortoolbar.h>

namespace
{
/// Группирует легко переставляемые параметры одного действия инструмента.
struct ToolDescription
{
  QString text;
  QString objectName;
  PolygonCanvasTool tool = PolygonCanvasTool::Select;
  QKeySequence shortcut;
};

/// Связывает действие с устойчивым видом инструмента через целочисленные данные Qt.
QAction * addTool(QToolBar * toolbar, QActionGroup * group, const ToolDescription & description)
{
  QAction * action = toolbar->addAction(description.text);
  action->setObjectName(description.objectName);
  action->setCheckable(true);
  action->setData(static_cast<int>(description.tool));
  action->setShortcut(description.shortcut);
  action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  group->addAction(action);
  return action;
}
} // namespace

/// Собирает компактную панель и применяет безопасные значения по умолчанию для первой установки.
PolygonEditorToolBar::PolygonEditorToolBar(QWidget * parent)
  : QWidget(parent)
  , selectAction_(nullptr)
  , outerAction_(nullptr)
  , holeAction_(nullptr)
  , lineAction_(nullptr)
  , arcAction_(nullptr)
  , bezierAction_(nullptr)
  , gridVisible_(new QCheckBox(tr("Сетка"), this))
  , gridStep_(new QDoubleSpinBox(this))
  , geometrySnap_(new QCheckBox(tr("Точки и центры"), this))
  , gridSnap_(new QCheckBox(tr("Привязка к сетке"), this))
  , clockwise_(new QCheckBox(tr("Дуга по часовой стрелке"), this))
{
  setObjectName(QStringLiteral("polygonEditorToolBar"));
  auto * toolbar = new QToolBar(tr("Инструменты редактора"), this);
  toolbar->setObjectName(QStringLiteral("polygonEditorTools"));
  toolbar->setMovable(false);
  auto * group = new QActionGroup(this);
  group->setExclusive(true);
  selectAction_ = addTool(toolbar, group,
                          {tr("Выбор"), QStringLiteral("editorSelectTool"), PolygonCanvasTool::Select, QKeySequence(Qt::Key_V)});
  outerAction_ =
    addTool(toolbar, group,
            {tr("Внешний контур"), QStringLiteral("editorOuterTool"), PolygonCanvasTool::OuterPath, QKeySequence(Qt::Key_O)});
  holeAction_ = addTool(toolbar, group,
                        {tr("Отверстие"), QStringLiteral("editorHoleTool"), PolygonCanvasTool::Hole, QKeySequence(Qt::Key_H)});
  lineAction_ =
    addTool(toolbar, group, {tr("Отрезок"), QStringLiteral("editorLineTool"), PolygonCanvasTool::Line, QKeySequence(Qt::Key_L)});
  arcAction_ =
    addTool(toolbar, group, {tr("Дуга"), QStringLiteral("editorArcTool"), PolygonCanvasTool::Arc, QKeySequence(Qt::Key_A)});
  bezierAction_ = addTool(
    toolbar, group, {tr("Bézier"), QStringLiteral("editorBezierTool"), PolygonCanvasTool::CubicBezier, QKeySequence(Qt::Key_B)});
  selectAction_->setChecked(true);

  gridVisible_->setObjectName(QStringLiteral("editorGridVisible"));
  gridStep_->setObjectName(QStringLiteral("editorGridStep"));
  geometrySnap_->setObjectName(QStringLiteral("editorGeometrySnap"));
  gridSnap_->setObjectName(QStringLiteral("editorGridSnap"));
  clockwise_->setObjectName(QStringLiteral("editorArcClockwiseTool"));
  gridStep_->setDecimals(3);
  gridStep_->setRange(0.001, 1.0e6);
  gridStep_->setSuffix(tr(" мм"));
  gridStep_->setAccessibleName(tr("Шаг сетки редактора"));

  QSettings settings;
  gridVisible_->setChecked(settings.value(QStringLiteral("editor/gridVisible"), true).toBool());
  bool validStep = false;
  const double storedStep = settings.value(QStringLiteral("editor/gridStepMm"), 10.0).toDouble(&validStep);
  gridStep_->setValue(validStep && std::isfinite(storedStep) ? std::clamp(storedStep, 0.001, 1.0e6) : 10.0);
  geometrySnap_->setChecked(settings.value(QStringLiteral("editor/snapGeometry"), true).toBool());
  gridSnap_->setChecked(settings.value(QStringLiteral("editor/snapGrid"), false).toBool());

  auto * layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 4);
  layout->addWidget(toolbar);
  layout->addSpacing(8);
  layout->addWidget(gridVisible_);
  layout->addWidget(new QLabel(tr("Шаг"), this));
  layout->addWidget(gridStep_);
  layout->addWidget(geometrySnap_);
  layout->addWidget(gridSnap_);
  layout->addWidget(clockwise_);
  layout->addStretch(1);

  connect(group, &QActionGroup::triggered, this,
          [this](QAction * action)
          {
            tool_ = static_cast<PolygonCanvasTool>(action->data().toInt());
            clockwise_->setVisible(tool_ == PolygonCanvasTool::Arc);
            emit toolChanged(tool_);
          });
  connect(gridVisible_, &QCheckBox::toggled, this, [this]() { publishSettings(); });
  connect(geometrySnap_, &QCheckBox::toggled, this, [this]() { publishSettings(); });
  connect(gridSnap_, &QCheckBox::toggled, this, [this]() { publishSettings(); });
  connect(gridStep_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { publishSettings(); });
  connect(clockwise_, &QCheckBox::toggled, this, &PolygonEditorToolBar::arcDirectionChanged);
  clockwise_->hide();
}

/// Возвращает локальное перечисление, не зависящее от прикладного слоя.
PolygonCanvasTool PolygonEditorToolBar::tool() const noexcept
{
  return tool_;
}

/// Собирает настройки только из уже проверенных элементов Qt.
PolygonCanvasSnapSettings PolygonEditorToolBar::snapSettings() const
{
  return {gridVisible_->isChecked(), gridStep_->value(), geometrySnap_->isChecked(), gridSnap_->isChecked()};
}

/// Читает явный пользовательский флаг направления дуги.
bool PolygonEditorToolBar::arcClockwise() const noexcept
{
  return clockwise_->isChecked();
}

/// Согласует перечисление с соответствующим проверяемым действием.
void PolygonEditorToolBar::selectTool(PolygonCanvasTool tool)
{
  const QList<QAction *> actions = {selectAction_, outerAction_, holeAction_, lineAction_, arcAction_, bezierAction_};
  for (QAction * action : actions)
    if (action->data().toInt() == static_cast<int>(tool))
    {
      action->setChecked(true);
      tool_ = tool;
      clockwise_->setVisible(tool_ == PolygonCanvasTool::Arc);
      emit toolChanged(tool_);
      return;
    }
}

/// Отключает только команды изменения, сохраняя просмотр сетки и текущий масштаб.
void PolygonEditorToolBar::setEditingEnabled(bool enabled)
{
  for (QAction * action : {outerAction_, holeAction_, lineAction_, arcAction_, bezierAction_})
    action->setEnabled(enabled);
  if (!enabled)
    selectTool(PolygonCanvasTool::Select);
}

/// Записывает четыре устойчивых ключа и немедленно обновляет полотно.
void PolygonEditorToolBar::publishSettings()
{
  QSettings settings;
  settings.setValue(QStringLiteral("editor/gridVisible"), gridVisible_->isChecked());
  settings.setValue(QStringLiteral("editor/gridStepMm"), gridStep_->value());
  settings.setValue(QStringLiteral("editor/snapGeometry"), geometrySnap_->isChecked());
  settings.setValue(QStringLiteral("editor/snapGrid"), gridSnap_->isChecked());
  emit snapSettingsChanged(snapSettings());
}
