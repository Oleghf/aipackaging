#ifndef AIPACKAGING_GUI_POLYGONSTARTPAGE_H
#define AIPACKAGING_GUI_POLYGONSTARTPAGE_H

#include <QStringList>
#include <QWidget>

#include <polygonworkspacepresentation.h>

class QListWidget;
class QPushButton;
class QLabel;

/// Показывает доступные способы начала работы без знания внутренних форматов.
class PolygonStartPage final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт стартовую страницу с недавними файлами и поставляемыми примерами.
  explicit PolygonStartPage(QWidget * parent = nullptr);
  /// Заменяет список недавних файлов и отмечает отсутствующие пути.
  void setRecentFiles(const QStringList & files);
  /// Заменяет список примеров парами «название, путь».
  void setExamples(const QList<QPair<QString, QString>> & examples);
  /// Показывает либо скрывает немодальную карточку автоматического восстановления.
  void setRecoveryCandidate(const PolygonRecoveryCandidate & recovery);
  /// Разрешает импорт DXF только после подключения прикладного сценария и при отсутствии поиска.
  void setImportEnabled(bool enabled);

signals:
  /// Запрашивает стандартный выбор задачи пользователем.
  void requestOpenProblem();
  /// Запрашивает создание нового пустого полигонального документа.
  void requestCreateDocument();
  /// Запрашивает открытие выбранного недавнего файла или примера.
  void requestOpenPath(const QString & path);
  /// Запрашивает удаление выбранного пути из списка недавних файлов.
  void requestRemoveRecent(const QString & path);
  /// Запрашивает восстановление читаемого автоматического черновика.
  void requestRestoreRecovery();
  /// Запрашивает удаление автоматического черновика после подтверждения пользователя.
  void requestDeleteRecovery();
  /// Запрашивает выбор исходного файла DXF.
  void requestImportDxf();

private:
  /// Открывает выбранный доступный элемент указанного списка.
  void openSelected(QListWidget * list);

  QListWidget * recentList_;
  QListWidget * exampleList_;
  QPushButton * removeRecentButton_;
  QWidget * recoveryCard_;
  QLabel * recoveryText_;
  QPushButton * restoreRecoveryButton_;
  QPushButton * importDxfButton_;
};

#endif
