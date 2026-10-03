#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <polygoncanvaswidget.h>
#include <polygondocumentpanel.h>
#include <polygoneditorpanel.h>
#include <polygoneditortoolbar.h>
#include <polygonproblemspanel.h>
#include <polygonrunpanel.h>
#include <polygonstatuspanel.h>
#include <polygonworkspacewidget.h>

/// Собирает прежнюю рабочую область из профильных панелей без изменения внешнего API.
PolygonWorkspaceWidget::PolygonWorkspaceWidget(QWidget * parent)
  : QWidget(parent)
  , documentPanel_(new PolygonDocumentPanel(this))
  , canvas_(new PolygonCanvasWidget(this))
  , editorToolBar_(new PolygonEditorToolBar(this))
  , editorPanel_(new PolygonEditorPanel(this))
  , problemsPanel_(new PolygonProblemsPanel(this))
  , runPanel_(new PolygonRunPanel(this))
  , rightTabs_(new QTabWidget(this))
  , statusPanel_(new PolygonStatusPanel(this))
  , splitter_(new QSplitter(Qt::Horizontal, this))
{
  setObjectName("polygonWorkspace");
  canvas_->setObjectName("polygonCanvas");
  canvas_->setAccessibleName(tr("Полотно раскладки"));
  auto * canvasContainer = new QWidget(this);
  auto * canvasLayout = new QVBoxLayout(canvasContainer);
  canvasLayout->setContentsMargins(0, 0, 0, 0);
  canvasLayout->addWidget(editorToolBar_);
  canvasLayout->addWidget(canvas_, 1);
  splitter_->setObjectName(QStringLiteral("workspaceSplitter"));
  splitter_->addWidget(documentPanel_);
  splitter_->addWidget(canvasContainer);
  rightTabs_->setObjectName(QStringLiteral("workspaceRightTabs"));
  rightTabs_->addTab(editorPanel_, tr("Редактор"));
  auto * runScroll = new QScrollArea(rightTabs_);
  runScroll->setObjectName(QStringLiteral("polygonRunScroll"));
  runScroll->setWidgetResizable(true);
  runScroll->setWidget(runPanel_);
  rightTabs_->addTab(runScroll, tr("Раскрой"));
  rightTabs_->addTab(problemsPanel_, tr("Проблемы"));
  splitter_->addWidget(rightTabs_);
  splitter_->setStretchFactor(0, 0);
  splitter_->setStretchFactor(1, 1);
  splitter_->setStretchFactor(2, 0);
  splitter_->setSizes({250, 720, 320});

  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->addWidget(splitter_, 1);
  layout->addWidget(statusPanel_);

  connect(runPanel_, &PolygonRunPanel::requestOpenProblem, this, &PolygonWorkspaceWidget::requestOpenProblem);
  connect(runPanel_, &PolygonRunPanel::requestSaveSolution, this, &PolygonWorkspaceWidget::requestSaveSolution);
  connect(runPanel_, &PolygonRunPanel::requestOpenModel, this, &PolygonWorkspaceWidget::requestOpenModel);
  connect(runPanel_, &PolygonRunPanel::requestCancelModelLoad, this, &PolygonWorkspaceWidget::requestCancelModelLoad);
  connect(runPanel_, &PolygonRunPanel::requestStart, this, &PolygonWorkspaceWidget::requestStart);
  connect(runPanel_, &PolygonRunPanel::requestCancel, this, &PolygonWorkspaceWidget::requestCancel);
  connect(runPanel_, &PolygonRunPanel::requestFit, canvas_, &PolygonCanvasWidget::fitToView);
  connect(canvas_, &PolygonCanvasWidget::cursorPositionChanged, this, &PolygonWorkspaceWidget::cursorPositionChanged);
  connect(canvas_, &PolygonCanvasWidget::partSelected, documentPanel_, &PolygonDocumentPanel::showSelectedPart);
  connect(editorPanel_, &PolygonEditorPanel::editRequested, this, &PolygonWorkspaceWidget::requestEditDocument);
  connect(editorPanel_, &PolygonEditorPanel::problemsRequested, this, [this]() { rightTabs_->setCurrentIndex(2); });
  connect(editorPanel_, &PolygonEditorPanel::entitySelected, canvas_, &PolygonCanvasWidget::selectEditorEntity);
  connect(editorPanel_, &PolygonEditorPanel::entitiesSelected, canvas_, &PolygonCanvasWidget::selectEditorEntities);
  connect(editorPanel_, &PolygonEditorPanel::entitiesSelected, problemsPanel_, &PolygonProblemsPanel::selectEntities);
  connect(canvas_, &PolygonCanvasWidget::editorEntitiesSelected, editorPanel_, &PolygonEditorPanel::selectEntities);
  connect(canvas_, &PolygonCanvasWidget::editorEntitiesSelected, problemsPanel_, &PolygonProblemsPanel::selectEntities);
  connect(problemsPanel_, &PolygonProblemsPanel::entitiesSelected, editorPanel_, &PolygonEditorPanel::selectEntities);
  connect(problemsPanel_, &PolygonProblemsPanel::entitiesSelected, canvas_, &PolygonCanvasWidget::selectEditorEntities);
  connect(problemsPanel_, &PolygonProblemsPanel::fixRequested, this, &PolygonWorkspaceWidget::requestEditDocument);
  connect(editorToolBar_, &PolygonEditorToolBar::toolChanged, canvas_, &PolygonCanvasWidget::setEditorTool);
  connect(editorToolBar_, &PolygonEditorToolBar::snapSettingsChanged, canvas_, &PolygonCanvasWidget::setSnapSettings);
  connect(editorToolBar_, &PolygonEditorToolBar::arcDirectionChanged, canvas_, &PolygonCanvasWidget::setArcClockwise);
  connect(canvas_, &PolygonCanvasWidget::editorToolChangeRequested, editorToolBar_, &PolygonEditorToolBar::selectTool);
  connect(canvas_, &PolygonCanvasWidget::editorInteractionMessage, this, &PolygonWorkspaceWidget::editorInteractionMessage);
  connect(rightTabs_, &QTabWidget::currentChanged, this,
          [this](int index)
          {
            const bool source = index != 1;
            editorToolBar_->setVisible(source);
            canvas_->setCanvasMode(source ? PolygonCanvasMode::Source : PolygonCanvasMode::Solution);
          });
  canvas_->setSnapSettings(editorToolBar_->snapSettings());
  addActions({startAction(), cancelAction(), fitAction()});
  qApp->installEventFilter(this);
  present({});
}

/// Ограничивает обработку своим окном и отдаёт модальным диалогам первенство.
bool PolygonWorkspaceWidget::eventFilter(QObject * watched, QEvent * event)
{
  auto * widget = qobject_cast<QWidget *>(watched);
  if (!widget || (widget != this && !isAncestorOf(widget)) || QApplication::activeModalWidget())
    return QWidget::eventFilter(watched, event);
  if (event->type() == QEvent::ShortcutOverride)
  {
    const auto * key = static_cast<QKeyEvent *>(event);
    if (key->key() == Qt::Key_F &&
        (qobject_cast<QLineEdit *>(widget) || qobject_cast<QTextEdit *>(widget) || qobject_cast<QPlainTextEdit *>(widget)))
    {
      event->accept();
      return true;
    }
  }
  if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
  {
    if (cancelAction()->isEnabled())
      cancelAction()->trigger();
    else
      cancelEditorInteraction();
    event->accept();
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

/// Делегирует формирование запроса панели запуска.
NestingRunRequest PolygonWorkspaceWidget::solverConfig() const
{
  return runPanel_->solverConfig();
}

/// Делегирует проверку полей панели запуска.
bool PolygonWorkspaceWidget::settingsValid() const
{
  return runPanel_->settingsValid();
}

/// Публикует один снимок во всех профильных представлениях в прежнем порядке.
void PolygonWorkspaceWidget::present(const PolygonWorkspaceSnapshot & snapshot)
{
  if (snapshot.documentRevision != presentedRevision_)
  {
    rightTabs_->setCurrentIndex(snapshot.documentDirty || !snapshot.documentValid ? 0 : 1);
    presentedRevision_ = snapshot.documentRevision;
  }
  else if (!snapshot.editableDocument && rightTabs_->currentWidget() == editorPanel_)
    rightTabs_->setCurrentIndex(1);
  documentPanel_->present(snapshot);
  editorPanel_->present(snapshot);
  problemsPanel_->present(snapshot);
  runPanel_->present(snapshot);
  statusPanel_->present(snapshot);
  canvas_->setSnapshot(snapshot);
  editorToolBar_->setEditingEnabled(snapshot.canEdit);
  rightTabs_->setTabText(2, snapshot.documentDiagnostics.empty() ? tr("Проблемы")
                                                                 : tr("Проблемы (%1)").arg(snapshot.documentDiagnostics.size()));
}

/// Копирует функции редактора в полотно для синхронных жестов владельца окна.
void PolygonWorkspaceWidget::setEditorActions(PolygonWorkspaceActions actions)
{
  canvas_->setEditorActions(std::move(actions));
}

/// Делегирует безопасный откат временного состояния полотну.
void PolygonWorkspaceWidget::cancelEditorInteraction()
{
  canvas_->cancelEditorInteraction();
}

/// Возвращает действие запуска панели запуска.
QAction * PolygonWorkspaceWidget::startAction() const
{
  return runPanel_->startAction();
}

/// Возвращает действие отмены панели запуска.
QAction * PolygonWorkspaceWidget::cancelAction() const
{
  return runPanel_->cancelAction();
}

/// Возвращает действие вписывания листа панели запуска.
QAction * PolygonWorkspaceWidget::fitAction() const
{
  return runPanel_->fitAction();
}

/// Делегирует выбор рекомендуемого режима панели запуска.
void PolygonWorkspaceWidget::selectRecommendedMode()
{
  runPanel_->selectRecommendedMode();
}

/// Делегирует восстановление пользовательского режима панели запуска.
void PolygonWorkspaceWidget::selectPreset(int preset)
{
  runPanel_->selectPreset(preset);
}

/// Возвращает сохранённый пользовательский режим панели запуска.
int PolygonWorkspaceWidget::selectedPreset() const
{
  return runPanel_->selectedPreset();
}

/// Делегирует переключение экспертной области панели запуска.
void PolygonWorkspaceWidget::setAdvancedExpanded(bool expanded)
{
  runPanel_->setAdvancedExpanded(expanded);
}

/// Возвращает состояние экспертной области панели запуска.
bool PolygonWorkspaceWidget::advancedExpanded() const
{
  return runPanel_->advancedExpanded();
}

/// Сериализует положение трёх рабочих панелей средствами Qt.
QByteArray PolygonWorkspaceWidget::splitterState() const
{
  return splitter_->saveState();
}

/// Применяет только непустое состояние внутренних панелей.
void PolygonWorkspaceWidget::restoreSplitterState(const QByteArray & state)
{
  if (!state.isEmpty())
    splitter_->restoreState(state);
}
