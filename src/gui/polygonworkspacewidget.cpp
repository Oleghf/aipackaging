#include <QAction>
#include <QSplitter>
#include <QVBoxLayout>

#include <polygoncanvaswidget.h>
#include <polygondocumentpanel.h>
#include <polygonrunpanel.h>
#include <polygonstatuspanel.h>
#include <polygonworkspacewidget.h>

/// Собирает прежнюю рабочую область из профильных панелей без изменения внешнего API.
PolygonWorkspaceWidget::PolygonWorkspaceWidget(QWidget * parent)
  : QWidget(parent)
  , documentPanel_(new PolygonDocumentPanel(this))
  , canvas_(new PolygonCanvasWidget(this))
  , runPanel_(new PolygonRunPanel(this))
  , statusPanel_(new PolygonStatusPanel(this))
  , splitter_(new QSplitter(Qt::Horizontal, this))
{
  setObjectName("polygonWorkspace");
  canvas_->setObjectName("polygonCanvas");
  canvas_->setAccessibleName(tr("Полотно раскладки"));
  splitter_->setObjectName(QStringLiteral("workspaceSplitter"));
  splitter_->addWidget(documentPanel_);
  splitter_->addWidget(canvas_);
  splitter_->addWidget(runPanel_);
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
  present({});
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
  documentPanel_->present(snapshot);
  runPanel_->present(snapshot);
  statusPanel_->present(snapshot);
  canvas_->setSnapshot(snapshot);
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
