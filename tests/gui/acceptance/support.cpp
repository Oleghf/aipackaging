#include "support.h"

#include <cstdlib>
#include <iostream>
#include <QAbstractButton>
#include <QAction>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollArea>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include <nesting_job_runner.h>
#include <polygon_artifact_store.h>
#include <polygon_backends.h>
#include <polygon_document_gateway.h>
#include <polygon_editable_document_gateway.h>
#include <polygon_import_jobs.h>
#include <polygon_model_jobs.h>
#include <qtapplicationdispatcher.h>

namespace acceptance
{
/// Ожидает проверяемое состояние, обслуживая очередь Qt без случайных задержек.
bool waitUntil(const std::function<bool()> & condition, int milliseconds)
{
  QElapsedTimer clock;
  clock.start();
  while (!condition() && clock.elapsed() < milliseconds)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  return condition();
}
/// Посылает пару событий мыши после предусмотренной прокрутки родительской формы.
void click(QWidget * target, QPoint point)
{
  if (!target || !target->isEnabled())
    throw std::runtime_error("Тестовая команда недоступна");
  for (QWidget * parent = target->parentWidget(); parent; parent = parent->parentWidget())
    if (auto * scroll = qobject_cast<QScrollArea *>(parent))
      scroll->ensureWidgetVisible(target);
  QCoreApplication::processEvents();
  if (point.isNull())
    point = target->rect().center();
  if (!target->isVisible() || !target->visibleRegion().contains(point))
  {
    const QRect available = target->visibleRegion().boundingRect();
    target->window()->grab().save(QStringLiteral(AIPACKAGING_SOURCE_ROOT "/build/gui-a1-wizard-failure.png"));
    for (QWidget * ancestor = target; ancestor; ancestor = ancestor->parentWidget())
      std::cerr << ancestor->metaObject()->className() << ' ' << ancestor->geometry().x() << ',' << ancestor->geometry().y()
                << ',' << ancestor->width() << ',' << ancestor->height() << '\n';
    throw std::runtime_error(
      QString("Точка нажатия недостижима после прокрутки: %1; видим=%2; точка=%3,%4; область=%5,%6,%7,%8; окно=%9,%10")
        .arg(target->objectName())
        .arg(target->isVisible())
        .arg(point.x())
        .arg(point.y())
        .arg(available.x())
        .arg(available.y())
        .arg(available.width())
        .arg(available.height())
        .arg(target->window()->width())
        .arg(target->window()->height())
        .toStdString());
  }
  QMouseEvent press(QEvent::MouseButtonPress, point, target->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(target, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, point, target->mapToGlobal(point), Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QApplication::sendEvent(target, &release);
  QCoreApplication::processEvents();
}
/// Доставляет клавишу через обычную обработку фокуса и сочетаний Qt.
void key(QWidget * target, int value, Qt::KeyboardModifiers modifiers, const QString & text)
{
  if (target->isWindow() && target->focusWidget())
    target = target->focusWidget();
  QApplication::setActiveWindow(target->window());
  target->setFocus();
  QCoreApplication::processEvents();
  QKeyEvent press(QEvent::KeyPress, value, modifiers, text);
  QApplication::sendEvent(target, &press);
  QKeyEvent release(QEvent::KeyRelease, value, modifiers, text);
  QApplication::sendEvent(target, &release);
  QCoreApplication::processEvents();
}
/// Заменяет поле последовательностью выбора всего текста, ввода и подтверждения.
void enter(QWidget * target, const QString & value)
{
  key(target, Qt::Key_A, Qt::ControlModifier);
  key(target, 0, {}, value);
  key(target, Qt::Key_Tab);
}
/// Ищет видимое представление действия, не вызывая обработчик приложения напрямую.
void action(PolygonMainWindow & window, const char * name)
{
  auto * command = widget<QAction>(window, name);
  if (!command->isEnabled())
    throw std::runtime_error(std::string("Действие недоступно: ") + name);
  for (auto * toolbar : window.findChildren<QToolBar *>())
    if (QWidget * button = toolbar->widgetForAction(command); button && button->isVisible())
    {
      click(button);
      return;
    }
  for (auto * menu : window.findChildren<QMenu *>())
    if (menu->actions().contains(command))
    {
      menu->popup(window.mapToGlobal(QPoint(20, 30)));
      QCoreApplication::processEvents();
      click(menu, menu->actionGeometry(command).center());
      return;
    }
  throw std::runtime_error("Действие не представлено кнопкой или меню");
}
/// Заполняет уже открывшийся вложенный диалог и нажимает его штатную кнопку.
void whenModal(std::function<void(QWidget *)> callback)
{
  auto * timer = new QTimer(qApp);
  auto clock = std::make_shared<QElapsedTimer>();
  clock->start();
  QObject::connect(timer, &QTimer::timeout, timer,
                   [timer, clock, callback = std::move(callback)]()
                   {
                     if (QWidget * dialog = QApplication::activeModalWidget())
                     {
                       timer->stop();
                       callback(dialog);
                       timer->deleteLater();
                     }
                     else if (clock->elapsed() > 10000)
                     {
                       std::cerr << "Не появилось ожидаемое модальное окно\n";
                       std::exit(2);
                     }
                   });
  timer->start(1);
}
/// Дожидается файлового диалога независимо от промежуточной доставки событий меню.
void chooseFile(const QString & path, bool cancel)
{
  auto * timer = new QTimer(qApp);
  auto clock = std::make_shared<QElapsedTimer>();
  clock->start();
  QObject::connect(timer, &QTimer::timeout, timer,
                   [timer, clock, path, cancel]()
                   {
                     auto * dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                     if (!dialog)
                     {
                       if (clock->elapsed() > 10000)
                       {
                         std::cerr << "Не появился ожидаемый файловый диалог\n";
                         std::exit(2);
                       }
                       return;
                     }
                     timer->stop();
                     timer->deleteLater();
                     if (cancel)
                     {
                       key(dialog, Qt::Key_Escape);
                       return;
                     }
                     auto * field = widget<QLineEdit>(*dialog, "fileNameEdit");
                     enter(field, path);
                     auto * buttons = dialog->findChild<QDialogButtonBox *>();
                     for (auto * button : buttons->buttons())
                       if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                       {
                         click(button);
                         return;
                       }
                     qFatal("Не найдена кнопка принятия файлового диалога");
                   });
  timer->start(1);
}
/// Читает файл без перекодирования для проверки сохранности байтов.
QByteArray bytes(const QString & path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Не удалось прочитать файл приёмки");
  return file.readAll();
}
/// Создаёт реальные шлюзы и фоновые исполнители с временем жизни корня композиции.
Desktop::Desktop(QString directory)
  : root(std::move(directory))
{
  auto output = std::shared_ptr<IPolygonWorkspaceOutput>(&window, [](IPolygonWorkspaceOutput *) {});
  auto importOutput = std::shared_ptr<IPolygonImportOutput>(&window, [](IPolygonImportOutput *) {});
  auto store = std::make_shared<PolygonArtifactStore>();
  auto documents = std::make_shared<LocalPolygonDocumentGateway>(store);
  auto editable = std::make_shared<LocalPolygonEditableDocumentGateway>(store);
  auto dispatcher = std::make_shared<QtApplicationDispatcher>(&window);
  auto baseline = std::make_shared<BaselinePolygonBackend>(store);
  std::shared_ptr<IPolygonModelJobRunner> models;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  models = std::make_shared<StdThreadPolygonModelJobRunner>(std::make_shared<LocalPolygonModelGateway>(store), dispatcher);
  auto backend = std::make_shared<PolygonBackendRouter>(baseline, std::make_shared<OnnxPolygonBackend>(store));
#else
  auto backend = std::make_shared<PolygonBackendRouter>(baseline);
#endif
  auto jobs = std::make_shared<StdThreadNestingJobRunner>(backend, dispatcher, documents);
  auto active = std::make_shared<ActivePolygonDocument>();
  workspace = std::make_shared<PolygonWorkspaceController>(output, documents, jobs, models, active);
  drafts = std::make_shared<StdThreadPolygonDraftJobRunner>(editable, dispatcher);
  document = std::make_shared<PolygonDocumentController>(editable, workspace, active,
                                                         (root + "/active.aipdraft.json").toStdString(), drafts);
  auto imports = std::make_shared<StdThreadPolygonImportJobRunner>(
    std::make_shared<LocalPolygonImportGateway>(editable, documents), dispatcher);
  importer = std::make_shared<PolygonImportController>(importOutput, imports, document);
  auto actions = workspace->actions();
  document->bindActions(actions);
  window.setPolygonWorkspaceActions(std::move(actions));
  window.setPolygonImportActions(importer->actions());
  window.resize(1280, 720);
  window.show();
  window.activateWindow();
  document->inspectRecovery();
  QCoreApplication::processEvents();
}
/// Освобождает контроллеры раньше окна, дожидаясь последовательного исполнителя записи.
Desktop::~Desktop()
{
  importer.reset();
  document.reset();
  workspace.reset();
  drafts.reset();
  QCoreApplication::processEvents();
}
/// Открывает документ через настоящий файловый диалог главного окна.
void Desktop::open(const QString & path)
{
  chooseFile(path);
  key(&window, Qt::Key_O, Qt::ControlModifier);
}
/// Применяет новое значение ширины штатной формой свойств.
void Desktop::changeWidth(double width)
{
  auto * tabs = widget<QTabWidget>(window, "workspaceRightTabs");
  click(tabs->tabBar(), tabs->tabBar()->tabRect(0).center());
  enter(widget<QDoubleSpinBox>(window, "editorSheetWidth"), QString::number(width));
  click(widget<QWidget>(window, "applyDocumentPropertiesButton"));
}
} // namespace acceptance
