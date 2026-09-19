#include <QFileDialog>
#include <QMenuBar>
#include <QSettings>
#include <utility>

#include <polygonmainwindow.h>
#include <polygonworkspacewidget.h>

/// Создаёт одно рабочее полотно и связывает его сигналы с действиями контроллера.
PolygonMainWindow::PolygonMainWindow(QWidget * parent)
  : QMainWindow(parent)
  , workspace_(new PolygonWorkspaceWidget(this))
  , openProblemAction_(nullptr)
  , saveSolutionAction_(nullptr)
  , openModelAction_(nullptr)
{
  setObjectName(QStringLiteral("polygonMainWindow"));
  setWindowTitle(tr("AIPackaging — полигональный раскрой"));
  setCentralWidget(workspace_);

  QMenu * fileMenu = menuBar()->addMenu(tr("Файл"));
  openProblemAction_ = fileMenu->addAction(tr("Открыть задачу"));
  saveSolutionAction_ = fileMenu->addAction(tr("Сохранить решение"));
  openModelAction_ = fileMenu->addAction(tr("Загрузить модель"));
  openProblemAction_->setShortcut(QKeySequence::Open);
  saveSolutionAction_->setShortcut(QKeySequence::Save);

  connect(openProblemAction_, &QAction::triggered, workspace_, &PolygonWorkspaceWidget::requestOpenProblem);
  connect(saveSolutionAction_, &QAction::triggered, workspace_, &PolygonWorkspaceWidget::requestSaveSolution);
  connect(openModelAction_, &QAction::triggered, workspace_, &PolygonWorkspaceWidget::requestOpenModel);

  connect(workspace_, &PolygonWorkspaceWidget::requestOpenProblem, this,
          [this]()
          {
            if (!actions_.openProblem)
              return;
            const QString path = QFileDialog::getOpenFileName(this, tr("Откройте полигональную задачу"), QString(),
                                                              tr("JSON (*.json);;Все файлы (*)"));
            if (!path.isEmpty())
              actions_.openProblem(path.toStdString());
          });
  connect(workspace_, &PolygonWorkspaceWidget::requestSaveSolution, this,
          [this]()
          {
            if (!actions_.saveSolution)
              return;
            const QString path =
              QFileDialog::getSaveFileName(this, tr("Сохраните полигональное решение"), QStringLiteral("polygon-solution.json"),
                                           tr("JSON (*.json);;Все файлы (*)"));
            if (!path.isEmpty())
              actions_.saveSolution(path.toStdString());
          });
  connect(workspace_, &PolygonWorkspaceWidget::requestOpenModel, this,
          [this]()
          {
            if (!actions_.openModel)
              return;
            const QString path = QFileDialog::getExistingDirectory(this, tr("Выберите каталог модели ONNX"));
            if (!path.isEmpty() && actions_.openModel(path.toStdString()))
              QSettings().setValue(QStringLiteral("polygon/modelDirectory"), path);
          });
  connect(workspace_, &PolygonWorkspaceWidget::requestStart, this,
          [this]()
          {
            if (actions_.start)
              actions_.start(workspace_->solverConfig());
          });
  connect(workspace_, &PolygonWorkspaceWidget::requestCancel, this,
          [this]()
          {
            if (actions_.cancel)
              actions_.cancel();
          });

  // До первого снимка контроллера файловое меню должно совпадать с пустым состоянием виджета.
  presentPolygonWorkspace({});
}

/// Сохраняет функции действий и повторно проверяет ранее выбранный комплект модели.
void PolygonMainWindow::setPolygonWorkspaceActions(PolygonWorkspaceActions actions)
{
  actions_ = std::move(actions);
  const QString remembered = QSettings().value(QStringLiteral("polygon/modelDirectory")).toString();
  if (!remembered.isEmpty() && actions_.openModel)
    actions_.openModel(remembered.toStdString());
}

/// Передаёт проверенный снимок виджету без преобразования геометрии.
void PolygonMainWindow::presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot)
{
  workspace_->present(snapshot);
  openProblemAction_->setEnabled(snapshot.canOpen);
  saveSolutionAction_->setEnabled(snapshot.canSave);
  openModelAction_->setEnabled(snapshot.canLoadModel);
}
