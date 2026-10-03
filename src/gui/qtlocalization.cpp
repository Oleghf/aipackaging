#include <QCoreApplication>
#include <QTranslator>

#include <qtlocalization.h>

/// Регистрирует встроенный ресурс и передаёт владение переводчиком приложению.
bool installRussianQtTranslation()
{
  Q_INIT_RESOURCE(polygon_translations);
  auto * app = QCoreApplication::instance();
  if (!app)
    return false;
  if (app->findChild<QTranslator *>(QStringLiteral("polygonQtTranslation")))
    return true;
  auto * translation = new QTranslator(app);
  translation->setObjectName(QStringLiteral("polygonQtTranslation"));
  if (!translation->load(QStringLiteral(":/translations/qtbase_ru.qm")) || !app->installTranslator(translation))
  {
    delete translation;
    return false;
  }
  return true;
}
