#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <polygonmainwindow.h>

/// Открывает локальный каталог справки; отсутствующие файлы диагностируются без обращения к сети.
void PolygonMainWindow::showHelp()
{
  const QString directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("docs/user"));
  const QString entry = QDir(directory).filePath(QStringLiteral("index.md"));
  if (!QFileInfo::exists(entry))
  {
    QMessageBox::warning(this, tr("Справка недоступна"),
                         tr("Не найдено локальное руководство. Распакуйте полный комплект приложения."));
    return;
  }
  auto * dialog = new QDialog(this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setObjectName(QStringLiteral("offlineHelpDialog"));
  dialog->setWindowTitle(tr("Руководство AIPackaging"));
  dialog->resize(850, 650);
  auto * layout = new QVBoxLayout(dialog);
  auto * browser = new QTextBrowser(dialog);
  browser->setObjectName(QStringLiteral("offlineHelpBrowser"));
  browser->setAccessibleName(tr("Автономное руководство пользователя"));
  browser->setOpenLinks(false);
  browser->setOpenExternalLinks(false);
  browser->setSource(QUrl::fromLocalFile(entry));
  connect(browser, &QTextBrowser::anchorClicked, dialog,
          [browser, directory](const QUrl & link)
          {
            const QUrl resolved = browser->source().resolved(link);
            const QString file = QFileInfo(resolved.toLocalFile()).canonicalFilePath();
            const QString root = QFileInfo(directory).canonicalFilePath() + QDir::separator();
            if (resolved.isLocalFile() && !file.isEmpty() && file.startsWith(root, Qt::CaseInsensitive))
              browser->setSource(resolved);
          });
  layout->addWidget(browser);
  auto * buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
  connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog->show();
}

/// Показывает проверяемые сведения сборки без изменения документа или выбранной модели.
void PolygonMainWindow::showAbout()
{
  QString text = tr("AIPackaging %1\nРевизия: %2\nЛокальный кандидат; не предназначен для управления станком.")
                   .arg(QStringLiteral(AIPACKAGING_PROJECT_VERSION), QStringLiteral(AIPACKAGING_BUILD_REVISION));
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  text += tr("\nПоддержка ONNX: включена");
#else
  text += tr("\nПоддержка ONNX: отсутствует");
#endif
  text += lastSnapshot_.modelReady
          ? tr("\nМодель: %1\nSHA-256: %2")
              .arg(QString::fromStdString(lastSnapshot_.modelId), QString::fromStdString(lastSnapshot_.modelSha256))
          : tr("\nПроверенная модель не подключена");
  QMessageBox::about(this, tr("О программе"), text);
}

/// Передаёт комплектную модель прежнему фоновому сценарию и не запоминает абсолютный путь поставки.
void PolygonMainWindow::useBundledModel()
{
  if (!actions_.openModel)
    return;
  pendingModelPath_ = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models/polygon-policy-v1"));
  pendingBundledModel_ = true;
  modelLoadTimer_.start();
  actions_.openModel(pendingModelPath_.toStdString());
}
