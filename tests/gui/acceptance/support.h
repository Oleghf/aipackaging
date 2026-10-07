#ifndef AIPACKAGING_GUI_ACCEPTANCE_SUPPORT_H
#define AIPACKAGING_GUI_ACCEPTANCE_SUPPORT_H

#include <functional>
#include <memory>
#include <QApplication>
#include <QString>
#include <QWidget>
#include <stdexcept>

#include <polygon_draft_jobs.h>
#include <polygondocumentcontroller.h>
#include <polygonimportcontroller.h>
#include <polygonmainwindow.h>
#include <polygonworkspacecontroller.h>

namespace acceptance
{
/// Доставляет события до выполнения условия либо возвращает отказ по ограниченному сроку.
bool waitUntil(const std::function<bool()> & condition, int milliseconds = 10000);
/// Посылает полный щелчок левой кнопкой в локальную точку виджета.
void click(QWidget * widget, QPoint point = {});
/// Посылает нажатие и отпускание клавиши с заданными модификаторами.
void key(QWidget * widget, int value, Qt::KeyboardModifiers modifiers = {}, const QString & text = {});
/// Вводит текст через события клавиатуры с заменой прежнего содержимого.
void enter(QWidget * widget, const QString & value);
/// Открывает действие через соответствующую кнопку панели или пункт меню.
void action(PolygonMainWindow & window, const char * name);
/// Подготавливает ответ следующему файловому диалогу через его поле и кнопку.
void chooseFile(const QString & path, bool cancel = false);
/// Выполняет действие только после появления модального окна, ограничивая ожидание десятью секундами.
void whenModal(std::function<void(QWidget *)> callback);
/// Возвращает байты файла либо выбрасывает диагностируемую ошибку чтения.
QByteArray bytes(const QString & path);
/// Возвращает именованный виджет либо выбрасывает ошибку устройства теста.
template<class T>
T * widget(QObject & root, const char * name)
{
  auto * result = root.findChild<T *>(QString::fromLatin1(name));
  if (!result)
    throw std::runtime_error(std::string("Не найден тестовый элемент: ") + name);
  return result;
}
/// Оборачивает только внешние операции для управляемых барьеров и отказов в приёмке.
struct DesktopHooks
{
  std::function<std::shared_ptr<IPolygonNestingBackend>(std::shared_ptr<IPolygonNestingBackend>)> backend;
  std::function<std::shared_ptr<IPolygonEditableDocumentGateway>(std::shared_ptr<IPolygonEditableDocumentGateway>)> editable;
  std::function<std::shared_ptr<IPolygonImportGateway>(std::shared_ptr<IPolygonImportGateway>)> imports;
  std::function<std::shared_ptr<IPolygonModelGateway>(std::shared_ptr<IPolygonModelGateway>)> models;
};
/// Владеет настоящим настольным сценарием в изолированном каталоге приёмки.
struct Desktop
{
  PolygonMainWindow window;
  std::shared_ptr<PolygonWorkspaceController> workspace;
  std::shared_ptr<PolygonDocumentController> document;
  std::shared_ptr<PolygonImportController> importer;
  std::shared_ptr<StdThreadPolygonDraftJobRunner> drafts;
  QString root;
  /// Подключает порты как в приложении, но использует только заданный каталог восстановления.
  explicit Desktop(QString directory, const DesktopHooks & hooks = {});
  /// Завершает контроллеры до уничтожения окна и отбрасывает поздние события.
  ~Desktop();
  /// Открывает задачу через сочетание окна и файловый диалог.
  void open(const QString & path);
  /// Меняет ширину через форму и кнопку применения без прямой редакторной команды.
  void changeWidth(double width);
};
/// Выполняет отдельную измерительную серию, возвращая ненулевой код при нарушении пределов.
int performance(const QString & root, int segments);
/// Выполняет сценарий отдельного процесса записи или восстановления тестового черновика.
int recoveryWorker(const QStringList & arguments);
/// Проверяет сохранение недавнего DXF между двумя изолированными процессами.
int recentWorker(const QStringList & arguments);
} // namespace acceptance
#endif
