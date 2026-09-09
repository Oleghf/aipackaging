#include <QLabel>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

#include <toastnotification.h>

namespace
{
constexpr int TOAST_WIDTH = 320;
}

//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
ToastNotification::ToastNotification(const QString & title, const QString & message, MessageType type, QWidget * parent)
  : QWidget(parent)
{
  setObjectName("toastNotification");
  setAttribute(Qt::WA_StyledBackground, true);
  setFixedWidth(TOAST_WIDTH);

  QLabel * titleLabel = new QLabel(title, this);
  titleLabel->setObjectName("toastTitle");
  titleLabel->setWordWrap(true);

  QLabel * messageLabel = new QLabel(message, this);
  messageLabel->setObjectName("toastMessage");
  messageLabel->setWordWrap(true);

  QVBoxLayout * layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 10, 14, 10);
  layout->setSpacing(4);
  layout->addWidget(titleLabel);
  layout->addWidget(messageLabel);

  applyStyle(type);
  adjustSize();
}


//------------------------------------------------------------------------------
/**
  Запускает таймер автоматического закрытия
*/
//--
void ToastNotification::startAutoClose(int durationMs)
{
  QTimer::singleShot(durationMs, this,
                     [this]()
                     {
                       close();
                       deleteLater();
                     });
}


//------------------------------------------------------------------------------
/**
  Настраивает цвет всплывающего сообщения
*/
//--
void ToastNotification::applyStyle(MessageType type)
{
  QString borderColor = "#2f6fed";
  QString backgroundColor = "#eef4ff";
  QString titleColor = "#173b7a";
  QString messageColor = "#1f2a44";

  switch (type)
  {
    case MessageType::Info:
      break;
    case MessageType::Warning:
      borderColor = "#d88900";
      backgroundColor = "#fff6df";
      titleColor = "#6b4300";
      messageColor = "#3f2f12";
      break;
    case MessageType::Error:
      borderColor = "#c92a2a";
      backgroundColor = "#fff0f0";
      titleColor = "#7a1010";
      messageColor = "#3d1717";
      break;
  }

  setStyleSheet(QString("QWidget#toastNotification {"
                        "background-color: %1;"
                        "border: 1px solid %2;"
                        "border-radius: 8px;"
                        "}"
                        "QLabel#toastTitle {"
                        "color: %3;"
                        "font-weight: 700;"
                        "font-size: 13px;"
                        "}"
                        "QLabel#toastMessage {"
                        "color: %4;"
                        "font-size: 12px;"
                        "}")
                  .arg(backgroundColor, borderColor, titleColor, messageColor));
}
