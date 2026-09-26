#ifndef AIPACKAGING_GUI_QTAPPLICATIONDISPATCHER_H
#define AIPACKAGING_GUI_QTAPPLICATIONDISPATCHER_H

#include <QPointer>

#include <polygonworkspaceview.h>

class QObject;

/// Ставит прикладные уведомления в очередь потока выбранного объекта Qt.
class QtApplicationDispatcher final : public IApplicationDispatcher
{
public:
  /// Сохраняет объект, поток которого будет принимать функции.
  explicit QtApplicationDispatcher(QObject * target);
  /// Ставит функцию в очередь Qt либо без исключения сообщает об отказе.
  bool post(std::function<void()> callback) noexcept override;

private:
  QPointer<QObject> target_;
};

#endif
