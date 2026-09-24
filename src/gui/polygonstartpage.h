#ifndef AIPACKAGING_GUI_POLYGONSTARTPAGE_H
#define AIPACKAGING_GUI_POLYGONSTARTPAGE_H

#include <QStringList>
#include <QWidget>

class QListWidget;
class QPushButton;

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

signals:
  /// Запрашивает стандартный выбор задачи пользователем.
  void requestOpenProblem();
  /// Запрашивает открытие выбранного недавнего файла или примера.
  void requestOpenPath(const QString & path);
  /// Запрашивает удаление выбранного пути из списка недавних файлов.
  void requestRemoveRecent(const QString & path);

private:
  /// Открывает выбранный доступный элемент указанного списка.
  void openSelected(QListWidget * list);

  QListWidget * recentList_;
  QListWidget * exampleList_;
  QPushButton * removeRecentButton_;
};

#endif
