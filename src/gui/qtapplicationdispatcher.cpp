#include <QMetaObject>
#include <QObject>

#include <qtapplicationdispatcher.h>

/// Сохраняет объект с временем жизни, превышающим время жизни диспетчера.
QtApplicationDispatcher::QtApplicationDispatcher(QObject * target)
  : target_(target)
{
}

/// Использует принудительно поставленное в очередь соединение для межпоточной доставки.
void QtApplicationDispatcher::post(std::function<void()> callback)
{
  QMetaObject::invokeMethod(target_, std::move(callback), Qt::QueuedConnection);
}
