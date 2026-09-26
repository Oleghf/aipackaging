#include <QMetaObject>
#include <QObject>

#include <qtapplicationdispatcher.h>

/// Сохраняет объект с временем жизни, превышающим время жизни диспетчера.
QtApplicationDispatcher::QtApplicationDispatcher(QObject * target)
  : target_(target)
{
}

/// Проверяет время жизни получателя и преобразует любой отказ Qt в `false`.
bool QtApplicationDispatcher::post(std::function<void()> callback) noexcept
{
  try
  {
    if (!target_ || !callback)
      return false;
    return QMetaObject::invokeMethod(
      target_.data(),
      [callback = std::move(callback)]() noexcept
      {
        try
        {
          callback();
        }
        catch (...)
        {
          // Исключение обработчика не должно покидать цикл событий Qt.
          return;
        }
      },
      Qt::QueuedConnection);
  }
  catch (...)
  {
    return false;
  }
}
