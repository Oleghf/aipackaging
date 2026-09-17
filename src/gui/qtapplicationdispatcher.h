#ifndef AIPACKAGING_GUI_QTAPPLICATIONDISPATCHER_H
#define AIPACKAGING_GUI_QTAPPLICATIONDISPATCHER_H

#include <polygonworkspaceview.h>

class QObject;

/// Ставит прикладные уведомления в очередь потока выбранного объекта Qt.
class QtApplicationDispatcher final : public IApplicationDispatcher
{
public:
  /// Сохраняет объект, поток которого будет принимать функции.
  explicit QtApplicationDispatcher(QObject * target);
  /// Ставит функцию в очередь Qt и никогда не выполняет её синхронно.
  void post(std::function<void()> callback) override;

private:
  QObject * target_;
};

#endif
