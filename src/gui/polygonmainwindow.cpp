#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <utility>

#include <polygondxfimportwizard.h>
#include <polygonmainwindow.h>
#include <polygonstartpage.h>
#include <polygonworkspacewidget.h>

namespace
{
constexpr qsizetype MAX_RECENT_PROBLEMS = 8;

/// Возвращает каталог поставляемых примеров рядом с исполняемым файлом.
QString exampleDirectory()
{
  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("examples/polygon"));
}
} // namespace

/// Создаёт стартовую и рабочую страницы, основные команды и строку состояния.
PolygonMainWindow::PolygonMainWindow(QWidget * parent)
  : QMainWindow(parent)
  , workspace_(new PolygonWorkspaceWidget(this))
  , startPage_(new PolygonStartPage(this))
  , pages_(new QStackedWidget(this))
  , coordinatesLabel_(new QLabel(tr("Координаты: —"), this))
  , homeAction_(nullptr)
  , openProblemAction_(nullptr)
  , createDocumentAction_(nullptr)
  , saveSolutionAction_(nullptr)
  , saveDocumentAction_(nullptr)
  , openModelAction_(nullptr)
  , importDxfAction_(nullptr)
  , forgetModelAction_(nullptr)
  , undoAction_(nullptr)
  , redoAction_(nullptr)
  , importWizard_(nullptr)
  , autosaveTimer_(new QTimer(this))
{
  buildWindow();
  connectActions();
  restoreUiState();
  firstWorkspaceDisplayTimer_.start();
  presentPolygonWorkspace({});
}

/// Создаёт страницы, меню, основные действия и общую панель инструментов.
void PolygonMainWindow::buildWindow()
{
  setObjectName(QStringLiteral("polygonMainWindow"));
  setWindowTitle(tr("AIPackaging — полигональный раскрой"));
  setMinimumSize(1280, 720);
  pages_->setObjectName(QStringLiteral("mainPages"));
  pages_->addWidget(startPage_);
  pages_->addWidget(workspace_);
  pages_->setCurrentWidget(startPage_);
  setCentralWidget(pages_);

  QMenu * fileMenu = menuBar()->addMenu(tr("&Файл"));
  homeAction_ = fileMenu->addAction(tr("&Начальная страница"));
  createDocumentAction_ = fileMenu->addAction(tr("&Создать задачу…"));
  openProblemAction_ = fileMenu->addAction(tr("&Открыть задачу…"));
  importDxfAction_ = fileMenu->addAction(tr("&Импортировать DXF…"));
  saveDocumentAction_ = fileMenu->addAction(tr("&Сохранить документ"));
  saveSolutionAction_ = fileMenu->addAction(tr("&Сохранить решение…"));
  fileMenu->addSeparator();
  openModelAction_ = fileMenu->addAction(tr("Подключить &модель…"));
  forgetModelAction_ = fileMenu->addAction(tr("Забыть модель"));
  openProblemAction_->setShortcut(QKeySequence::Open);
  saveDocumentAction_->setShortcut(QKeySequence::Save);
  saveSolutionAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  openProblemAction_->setObjectName(QStringLiteral("openProblemAction"));
  createDocumentAction_->setObjectName(QStringLiteral("createDocumentAction"));
  importDxfAction_->setObjectName(QStringLiteral("importDxfAction"));
  saveSolutionAction_->setObjectName(QStringLiteral("saveSolutionAction"));
  saveDocumentAction_->setObjectName(QStringLiteral("saveDocumentAction"));
  openModelAction_->setObjectName(QStringLiteral("openModelAction"));

  QMenu * editMenu = menuBar()->addMenu(tr("&Правка"));
  undoAction_ = editMenu->addAction(tr("&Отменить"));
  redoAction_ = editMenu->addAction(tr("&Повторить"));
  undoAction_->setShortcuts({QKeySequence::Undo});
  redoAction_->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
  undoAction_->setObjectName(QStringLiteral("undoDocumentAction"));
  redoAction_->setObjectName(QStringLiteral("redoDocumentAction"));

  auto * toolbar = addToolBar(tr("Основные команды"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  toolbar->setMovable(false);
  toolbar->addAction(homeAction_);
  toolbar->addAction(createDocumentAction_);
  toolbar->addAction(openProblemAction_);
  toolbar->addAction(importDxfAction_);
  toolbar->addAction(saveDocumentAction_);
  toolbar->addAction(saveSolutionAction_);
  toolbar->addAction(undoAction_);
  toolbar->addAction(redoAction_);
  toolbar->addSeparator();
  toolbar->addAction(openModelAction_);
  toolbar->addSeparator();
  toolbar->addAction(workspace_->startAction());
  toolbar->addAction(workspace_->cancelAction());
  toolbar->addAction(workspace_->fitAction());

  statusBar()->addPermanentWidget(coordinatesLabel_);
  coordinatesLabel_->setObjectName(QStringLiteral("cursorCoordinates"));
  coordinatesLabel_->setAccessibleName(tr("Координаты курсора на листе"));
  autosaveTimer_->setSingleShot(true);
  autosaveTimer_->setInterval(2000);
  autosaveTimer_->setObjectName(QStringLiteral("polygonAutosaveTimer"));
}

/// Соединяет действия окна с прикладными функциями и сигналами вложенных страниц.
void PolygonMainWindow::connectActions()
{
  connect(homeAction_, &QAction::triggered, this, [this]() { pages_->setCurrentWidget(startPage_); });
  connect(createDocumentAction_, &QAction::triggered, this, &PolygonMainWindow::createDocument);
  connect(openProblemAction_, &QAction::triggered, this, &PolygonMainWindow::chooseProblem);
  connect(importDxfAction_, &QAction::triggered, this, &PolygonMainWindow::chooseDxf);
  connect(saveDocumentAction_, &QAction::triggered, this, &PolygonMainWindow::saveDocument);
  connect(saveSolutionAction_, &QAction::triggered, this, &PolygonMainWindow::saveSolution);
  connect(openModelAction_, &QAction::triggered, this, &PolygonMainWindow::chooseModel);
  connect(forgetModelAction_, &QAction::triggered, this, &PolygonMainWindow::forgetModel);
  connect(undoAction_, &QAction::triggered, this,
          [this]()
          {
            if (actions_.undoDocument)
              actions_.undoDocument();
          });
  connect(redoAction_, &QAction::triggered, this,
          [this]()
          {
            if (actions_.redoDocument)
              actions_.redoDocument();
          });
  connect(startPage_, &PolygonStartPage::requestOpenProblem, this, &PolygonMainWindow::chooseProblem);
  connect(startPage_, &PolygonStartPage::requestCreateDocument, this, &PolygonMainWindow::createDocument);
  connect(startPage_, &PolygonStartPage::requestOpenPath, this, &PolygonMainWindow::openPath);
  connect(startPage_, &PolygonStartPage::requestRemoveRecent, this, &PolygonMainWindow::removeRecentProblem);
  connect(startPage_, &PolygonStartPage::requestRestoreRecovery, this,
          [this]()
          {
            if (confirmDocumentReplacement() && actions_.restoreRecovery)
              actions_.restoreRecovery();
          });
  connect(startPage_, &PolygonStartPage::requestDeleteRecovery, this,
          [this]()
          {
            if (actions_.deleteRecovery)
              actions_.deleteRecovery();
          });
  connect(startPage_, &PolygonStartPage::requestImportDxf, this, &PolygonMainWindow::chooseDxf);
  connect(workspace_, &PolygonWorkspaceWidget::requestOpenProblem, this, &PolygonMainWindow::chooseProblem);
  connect(workspace_, &PolygonWorkspaceWidget::requestSaveSolution, saveSolutionAction_, &QAction::trigger);
  connect(workspace_, &PolygonWorkspaceWidget::requestOpenModel, openModelAction_, &QAction::trigger);
  connect(workspace_, &PolygonWorkspaceWidget::requestCancelModelLoad, this, &PolygonMainWindow::cancelModelLoad);
  connect(workspace_, &PolygonWorkspaceWidget::requestStart, this, &PolygonMainWindow::startRun);
  connect(workspace_, &PolygonWorkspaceWidget::requestCancel, this, &PolygonMainWindow::cancelRun);
  connect(workspace_, &PolygonWorkspaceWidget::requestEditDocument, this,
          [this](const aipackaging::editor::EditorCommandBatch & batch)
          {
            if (actions_.editDocument)
              actions_.editDocument(batch);
          });
  connect(workspace_, &PolygonWorkspaceWidget::cursorPositionChanged, this, &PolygonMainWindow::showCursorPosition);
  connect(workspace_, &PolygonWorkspaceWidget::editorInteractionMessage, this,
          [this](const QString & message) { statusBar()->showMessage(message, 5000); });
  connect(autosaveTimer_, &QTimer::timeout, this,
          [this]()
          {
            if (actions_.autosaveDocument)
              actions_.autosaveDocument();
          });
}

/// Показывает один диалог обязательных полей и создаёт документ только после подтверждения пользователя.
void PolygonMainWindow::createDocument()
{
  if (!actions_.createDocument || !confirmDocumentReplacement())
    return;
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Создание полигональной задачи"));
  auto * identifier = new QLineEdit(&dialog);
  identifier->setObjectName(QStringLiteral("newProblemId"));
  auto makeSize = [&dialog]()
  {
    auto * value = new QDoubleSpinBox(&dialog);
    value->setDecimals(3);
    value->setRange(0.001, 1.0e9);
    value->setSuffix(QObject::tr(" мм"));
    return value;
  };
  QDoubleSpinBox * width = makeSize();
  QDoubleSpinBox * height = makeSize();
  width->setObjectName(QStringLiteral("newSheetWidth"));
  height->setObjectName(QStringLiteral("newSheetHeight"));
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto * form = new QFormLayout(&dialog);
  form->addRow(tr("Идентификатор задачи"), identifier);
  form->addRow(tr("Ширина листа"), width);
  form->addRow(tr("Высота листа"), height);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog,
          [&dialog, identifier]()
          {
            if (!identifier->text().trimmed().isEmpty())
              dialog.accept();
          });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted)
    actions_.createDocument(identifier->text().trimmed().toStdString(), width->value(), height->value());
}

/// Выбирает путь рядом с последним каталогом и сохраняет только при наличии прикладного действия.
void PolygonMainWindow::saveSolution()
{
  if (!actions_.saveSolution)
    return;
  QSettings settings;
  const QString directory = settings.value(QStringLiteral("files/lastDirectory")).toString();
  const QString path = QFileDialog::getSaveFileName(this, tr("Сохраните полигональное решение"),
                                                    QDir(directory).filePath(QStringLiteral("polygon-solution.json")),
                                                    tr("JSON (*.json);;Все файлы (*)"));
  if (path.isEmpty())
    return;
  settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
  actions_.saveSolution(path.toStdString());
}

/// Выбирает прямое сохранение известного источника либо новый путь для восстановленного или некорректного документа.
void PolygonMainWindow::saveDocument()
{
  if (!actions_.saveDocument || !lastSnapshot_.hasDocument)
    return;
  QString path = QString::fromStdString(lastSnapshot_.document.sourceIdentifier);
  bool asDraft = lastSnapshot_.documentSource == PolygonDocumentSource::Draft;
  const bool needsNewPath = path.isEmpty() || lastSnapshot_.documentSource == PolygonDocumentSource::RecoveredDraft ||
                            lastSnapshot_.documentSource == PolygonDocumentSource::Imported ||
                            lastSnapshot_.documentSource == PolygonDocumentSource::Untitled || !lastSnapshot_.documentValid;
  if (needsNewPath)
  {
    QSettings settings;
    const QString directory = settings.value(QStringLiteral("files/lastDirectory")).toString();
    const bool importedProblem = (lastSnapshot_.documentSource == PolygonDocumentSource::Imported ||
                                  lastSnapshot_.documentSource == PolygonDocumentSource::Untitled) &&
                                 lastSnapshot_.documentValid;
    path = QFileDialog::getSaveFileName(
      this,
      importedProblem ? tr("Сохраните импортированную полигональную задачу") : tr("Сохраните черновик полигональной задачи"),
      QDir(directory).filePath(importedProblem ? QStringLiteral("polygon-problem.json")
                                               : QStringLiteral("polygon-document.aipdraft.json")),
      importedProblem ? tr("Полигональная задача (*.json);;Все файлы (*)")
                      : tr("Черновик AIPackaging (*.aipdraft.json);;JSON (*.json);;Все файлы (*)"));
    if (path.isEmpty())
      return;
    asDraft = !importedProblem;
    settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
  }
  actions_.saveDocument(path.toStdString(), asDraft);
}

/// Запоминает выбранный каталог как ожидающий проверки и запускает прикладное действие модели.
void PolygonMainWindow::chooseModel()
{
  if (!actions_.openModel)
    return;
  QSettings settings;
  const QString initial = settings.value(QStringLiteral("model/lastDirectory")).toString();
  const QString path = QFileDialog::getExistingDirectory(this, tr("Выберите каталог модели ONNX"), initial);
  if (path.isEmpty())
    return;
  pendingModelPath_ = path;
  modelLoadTimer_.start();
  actions_.openModel(path.toStdString());
}

/// Освобождает модель через прикладное действие и очищает только постоянный путь Qt.
void PolygonMainWindow::forgetModel()
{
  if (actions_.forgetModel)
    actions_.forgetModel();
  QSettings().remove(QStringLiteral("polygon/modelDirectory"));
}

/// Удаляет все совпадения пути, сохраняет список и немедленно обновляет стартовую страницу.
void PolygonMainWindow::removeRecentProblem(const QString & path)
{
  recentProblems_.removeAll(path);
  QSettings().setValue(QStringLiteral("files/recentProblems"), recentProblems_);
  startPage_->setRecentFiles(recentProblems_);
}

/// Передаёт прикладному слою проверенный панелью запрос текущего режима.
void PolygonMainWindow::startRun()
{
  workspace_->cancelEditorInteraction();
  if (actions_.start)
    actions_.start(workspace_->solverConfig());
}

/// Вызывает прикладную отмену только при настроенном действии.
void PolygonMainWindow::cancelRun()
{
  if (actions_.cancel)
    actions_.cancel();
}

/// Вызывает прикладную отмену проверки модели только при настроенном действии.
void PolygonMainWindow::cancelModelLoad()
{
  if (actions_.cancelModelLoad)
    actions_.cancelModelLoad();
}

/// Форматирует миллиметры с двумя знаками либо показывает отсутствие координат.
void PolygonMainWindow::showCursorPosition(double x, double y, bool inside)
{
  coordinatesLabel_->setText(inside ? tr("X: %1 мм; Y: %2 мм").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2) : tr("Координаты: —"));
}

/// Сохраняет функции действий и запускает фоновую проверку ранее выбранной модели.
void PolygonMainWindow::setPolygonWorkspaceActions(PolygonWorkspaceActions actions)
{
  actions_ = std::move(actions);
  workspace_->setEditorActions(actions_);
  const QString remembered = QSettings().value(QStringLiteral("polygon/modelDirectory")).toString();
  if (!remembered.isEmpty() && actions_.openModel)
  {
    pendingModelPath_ = remembered;
    modelLoadTimer_.start();
    actions_.openModel(remembered.toStdString());
  }
}

/// Сохраняет независимые действия импорта и открывает доступ к мастеру на стартовой странице.
void PolygonMainWindow::setPolygonImportActions(PolygonImportActions actions)
{
  importActions_ = std::move(actions);
  const bool enabled = static_cast<bool>(importActions_.inspect) && lastSnapshot_.canOpen;
  importDxfAction_->setEnabled(enabled);
  startPage_->setImportEnabled(enabled);
}

/// Передаёт снимок рабочей странице и согласует навигацию, недавние файлы и модель.
void PolygonMainWindow::presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot)
{
  const bool scheduleAutosave =
    snapshot.documentDirty &&
    (!lastSnapshot_.documentDirty || snapshot.document.sourceIdentifier != lastSnapshot_.document.sourceIdentifier ||
     snapshot.documentRevision != lastSnapshot_.documentRevision);
  lastSnapshot_ = snapshot;
  workspace_->present(snapshot);
  startPage_->setRecoveryCandidate(snapshot.recovery);
  presentActions(snapshot);
  presentDocumentNavigation(snapshot);
  presentModelLoad(snapshot);
  if (scheduleAutosave)
    autosaveTimer_->start();
  else if (!snapshot.documentDirty)
    autosaveTimer_->stop();
}

/// Передаёт состояние только существующему мастеру; фоновая работа не создаёт окон самостоятельно.
void PolygonMainWindow::presentPolygonImport(const PolygonImportSnapshot & snapshot)
{
  if (importWizard_)
    importWizard_->present(snapshot);
}

/// Обновляет доступность общих действий и строку состояния окна.
void PolygonMainWindow::presentActions(const PolygonWorkspaceSnapshot & snapshot)
{
  openProblemAction_->setEnabled(snapshot.canOpen);
  createDocumentAction_->setEnabled(snapshot.canOpen);
  saveSolutionAction_->setEnabled(snapshot.canSave);
  saveDocumentAction_->setEnabled(snapshot.canSaveDocument);
  openModelAction_->setEnabled(snapshot.canLoadModel);
  const bool importEnabled = snapshot.canOpen && static_cast<bool>(importActions_.inspect);
  importDxfAction_->setEnabled(importEnabled);
  startPage_->setImportEnabled(importEnabled);
  undoAction_->setEnabled(snapshot.canUndo);
  redoAction_->setEnabled(snapshot.canRedo);
  undoAction_->setText(snapshot.undoLabel.empty() ? tr("&Отменить")
                                                  : tr("&Отменить: %1").arg(QString::fromStdString(snapshot.undoLabel)));
  redoAction_->setText(snapshot.redoLabel.empty() ? tr("&Повторить")
                                                  : tr("&Повторить: %1").arg(QString::fromStdString(snapshot.redoLabel)));
  statusBar()->showMessage(QString::fromStdString(snapshot.statusText));
}

/// Переключает рабочую страницу и завершает учёт успешно открытого документа.
void PolygonMainWindow::presentDocumentNavigation(const PolygonWorkspaceSnapshot & snapshot)
{
  if (snapshot.hasDocument || !snapshot.problemId.empty())
  {
    pages_->setCurrentWidget(workspace_);
    const QString loadedPath = QString::fromStdString(snapshot.document.sourceIdentifier);
    if (!loadedPath.isEmpty())
      rememberProblem(loadedPath);
    pendingProblemPath_.clear();
    if (documentLoadTimer_.isValid())
    {
      qInfo() << "document_load_ms" << documentLoadTimer_.elapsed();
      documentLoadTimer_.invalidate();
    }
    if (!firstWorkspaceShown_)
    {
      firstWorkspaceShown_ = true;
      // Нулевая задержка переносит измерение за обработку переключения страницы и первой перерисовки.
      QTimer::singleShot(0, this,
                         [this]()
                         {
                           qInfo() << "first_workspace_display_ms" << firstWorkspaceDisplayTimer_.elapsed();
                           firstWorkspaceDisplayTimer_.invalidate();
                         });
    }
  }
  else if (snapshot.state == PolygonWorkspaceState::Empty || snapshot.state == PolygonWorkspaceState::Error)
    pages_->setCurrentWidget(startPage_);
}

/// Сохраняет только успешно проверенный путь модели и очищает состояние неудачной попытки.
void PolygonMainWindow::presentModelLoad(const PolygonWorkspaceSnapshot & snapshot)
{
  if (snapshot.modelState == PolygonModelState::Ready && !pendingModelPath_.isEmpty())
  {
    if (modelLoadTimer_.isValid())
    {
      qInfo() << "model_load_ms" << modelLoadTimer_.elapsed();
      modelLoadTimer_.invalidate();
    }
    QSettings settings;
    settings.setValue(QStringLiteral("polygon/modelDirectory"), pendingModelPath_);
    settings.setValue(QStringLiteral("model/lastDirectory"), pendingModelPath_);
    pendingModelPath_.clear();
    if (initialModelChoicePending_)
      workspace_->selectRecommendedMode();
  }
  if (snapshot.modelState == PolygonModelState::Error)
  {
    modelLoadTimer_.invalidate();
    pendingModelPath_.clear();
  }
  initialModelChoicePending_ = initialModelChoicePending_ && snapshot.state == PolygonWorkspaceState::Empty;
}

/// Сохраняет настройки рабочего места перед штатным закрытием.
void PolygonMainWindow::closeEvent(QCloseEvent * event)
{
  if (autosaveTimer_->isActive() && actions_.autosaveDocument)
  {
    autosaveTimer_->stop();
    actions_.autosaveDocument();
  }
  if (!confirmDocumentReplacement())
  {
    event->ignore();
    return;
  }
  QSettings settings;
  settings.setValue(QStringLiteral("ui/mainWindowGeometry"), saveGeometry());
  settings.setValue(QStringLiteral("ui/mainWindowState"), saveState());
  settings.setValue(QStringLiteral("ui/expertExpanded"), workspace_->advancedExpanded());
  settings.setValue(QStringLiteral("ui/workspaceSplitter"), workspace_->splitterState());
  settings.setValue(QStringLiteral("ui/runPreset"), workspace_->selectedPreset());
  QMainWindow::closeEvent(event);
}

/// Открывает диалог в последнем каталоге и передаёт выбранный путь общему сценарию.
void PolygonMainWindow::chooseProblem()
{
  QSettings settings;
  const QString directory = settings.value(QStringLiteral("files/lastDirectory")).toString();
  const QString path = QFileDialog::getOpenFileName(this, tr("Откройте полигональную задачу"), directory,
                                                    tr("Задачи и черновики (*.json *.aipdraft.json);;Все файлы (*)"));
  if (!path.isEmpty())
    openPath(path);
}

/// Выбирает обычный файл DXF и запускает отдельный мастер без замены текущего документа.
void PolygonMainWindow::chooseDxf()
{
  if (!importActions_.inspect || importWizard_ || !confirmDocumentReplacement())
    return;
  QSettings settings;
  const QString directory = settings.value(QStringLiteral("files/lastDirectory")).toString();
  const QString path =
    QFileDialog::getOpenFileName(this, tr("Импортируйте ASCII DXF"), directory, tr("Файлы DXF (*.dxf);;Все файлы (*)"));
  if (path.isEmpty())
    return;
  settings.setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
  importWizard_ = new PolygonDxfImportWizard(this);
  importWizard_->setAttribute(Qt::WA_DeleteOnClose);
  connect(importWizard_, &QObject::destroyed, this, [this]() { importWizard_ = nullptr; });
  importWizard_->startImport(path, importActions_);
  importWizard_->show();
  importWizard_->raise();
}

/// Запоминает каталог, но добавляет путь в недавние только после успешного снимка.
void PolygonMainWindow::openPath(const QString & path)
{
  if (path.isEmpty() || !actions_.openProblem || !confirmDocumentReplacement())
    return;
  pendingProblemPath_ = path;
  documentLoadTimer_.start();
  QSettings().setValue(QStringLiteral("files/lastDirectory"), QFileInfo(path).absolutePath());
  actions_.openProblem(path.toStdString());
}

/// Выполняет выбранное пользователем действие над грязным документом и сообщает, можно ли продолжать.
bool PolygonMainWindow::confirmDocumentReplacement()
{
  if (!lastSnapshot_.documentDirty)
    return true;
  const QMessageBox::StandardButton choice =
    QMessageBox::warning(this, tr("Несохранённые изменения"), tr("Сохранить изменения активного документа?"),
                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
  if (choice == QMessageBox::Cancel)
    return false;
  if (choice == QMessageBox::Save)
  {
    saveDocument();
    return !lastSnapshot_.documentDirty;
  }
  if (actions_.deleteRecovery)
    actions_.deleteRecovery();
  return true;
}

/// Перемещает путь в начало ограниченного списка и обновляет стартовую страницу.
void PolygonMainWindow::rememberProblem(const QString & path)
{
  if (!QFileInfo(path).isFile())
    return;
  recentProblems_.removeAll(path);
  recentProblems_.prepend(path);
  while (recentProblems_.size() > MAX_RECENT_PROBLEMS)
    recentProblems_.removeLast();
  QSettings().setValue(QStringLiteral("files/recentProblems"), recentProblems_);
  startPage_->setRecentFiles(recentProblems_);
}

/// Восстанавливает геометрию, пользовательские панели, режим и каталоги примеров.
void PolygonMainWindow::restoreUiState()
{
  QSettings settings;
  restoreGeometry(settings.value(QStringLiteral("ui/mainWindowGeometry")).toByteArray());
  restoreState(settings.value(QStringLiteral("ui/mainWindowState")).toByteArray());
  recentProblems_ = settings.value(QStringLiteral("files/recentProblems")).toStringList();
  while (recentProblems_.size() > MAX_RECENT_PROBLEMS)
    recentProblems_.removeLast();
  settings.setValue(QStringLiteral("files/recentProblems"), recentProblems_);
  startPage_->setRecentFiles(recentProblems_);
  const QString examples = exampleDirectory();
  startPage_->setExamples({{tr("Простой раскрой"), QDir(examples).filePath(QStringLiteral("problem-small.json"))},
                           {tr("Вогнутая деталь"), QDir(examples).filePath(QStringLiteral("problem-concave.json"))},
                           {tr("Кривые и отверстие"), QDir(examples).filePath(QStringLiteral("problem-curves-and-hole.json"))}});
  workspace_->setAdvancedExpanded(settings.value(QStringLiteral("ui/expertExpanded"), false).toBool());
  workspace_->restoreSplitterState(settings.value(QStringLiteral("ui/workspaceSplitter")).toByteArray());
  workspace_->selectPreset(settings.value(QStringLiteral("ui/runPreset"), 0).toInt());
}
