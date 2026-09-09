#ifndef TOASTNOTIFICATION_H__
#define TOASTNOTIFICATION_H__

#include <QWidget>

#include <iview.h>

class QString;

////////////////////////////////////////////////////////////////////////////////
//
/// Неблокирующее всплывающее сообщение
/**
*/
////////////////////////////////////////////////////////////////////////////////
class ToastNotification : public QWidget
{
public:
  // Конструктор
  ToastNotification(const QString & title, const QString & message, MessageType type, QWidget * parent = nullptr);

  // Запустить автоматическое закрытие
  void startAutoClose(int durationMs);

private:
  // Применить визуальный стиль для типа сообщения
  void applyStyle(MessageType type);
};

#endif
