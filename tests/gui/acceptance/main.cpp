#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <qtlocalization.h>

#include "support.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

/// Изолирует настройки до первого окна и запускает приёмку либо измерительную серию.
int main(int argc, char ** argv)
{
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
  QApplication application(argc, argv);
  QTemporaryDir profile;
  if (!profile.isValid())
    return 2;
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("AIPackagingAcceptance"));
  QCoreApplication::setApplicationName(QStringLiteral("IsolatedGuiAcceptance"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, profile.path());
  installRussianQtTranslation();
  if (application.arguments().contains(QStringLiteral("--recovery-worker")))
    return acceptance::recoveryWorker(application.arguments());
  if (application.arguments().contains(QStringLiteral("--performance")))
    return acceptance::performance(profile.path(), application.arguments().last().toInt());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
