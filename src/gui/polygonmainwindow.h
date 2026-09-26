#ifndef AIPACKAGING_GUI_POLYGONMAINWINDOW_H
#define AIPACKAGING_GUI_POLYGONMAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>

#include <polygonworkspaceview.h>

class PolygonWorkspaceWidget;
class QAction;
class QCloseEvent;
class QLabel;
class QStackedWidget;
class PolygonStartPage;

/// Показывает только полигональный раскрой и передаёт действия прикладному контроллеру.
class PolygonMainWindow final : public QMainWindow,
                                public IPolygonWorkspaceOutput
{
  Q_OBJECT
public:
  /// Создаёт полигональную рабочую область без унаследованного клеточного редактора.
  explicit PolygonMainWindow(QWidget * parent = nullptr);

  /// Подключает действия контроллера и восстанавливает ранее проверенный путь модели.
  void setPolygonWorkspaceActions(PolygonWorkspaceActions actions);

  /// Отображает согласованный снимок состояния полигональной рабочей области.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) override;

protected:
  /// Сохраняет геометрию и расположение панелей перед закрытием окна.
  void closeEvent(QCloseEvent * event) override;

private:
  /// Собирает страницы, основные действия, панель инструментов и строку состояния.
  void buildWindow();
  /// Связывает команды окна, стартовой страницы и рабочей области.
  void connectActions();
  /// Согласует доступность общих действий с прикладным снимком.
  void presentActions(const PolygonWorkspaceSnapshot & snapshot);
  /// Переключает страницы и учитывает успешно открытый документ.
  void presentDocumentNavigation(const PolygonWorkspaceSnapshot & snapshot);
  /// Завершает измерение и сохранение пути успешно проверенной модели.
  void presentModelLoad(const PolygonWorkspaceSnapshot & snapshot);
  /// Открывает файловый диалог из последнего пользовательского каталога.
  void chooseProblem();
  /// Передаёт выбранный путь приложению и запоминает каталог.
  void openPath(const QString & path);
  /// Добавляет только успешно загруженный путь в начало списка недавних задач.
  void rememberProblem(const QString & path);
  /// Восстанавливает пользовательское состояние окна.
  void restoreUiState();

  PolygonWorkspaceWidget * workspace_;
  PolygonStartPage * startPage_;
  QStackedWidget * pages_;
  QLabel * coordinatesLabel_;
  QAction * homeAction_;
  QAction * openProblemAction_;
  QAction * saveSolutionAction_;
  QAction * openModelAction_;
  QAction * forgetModelAction_;
  PolygonWorkspaceActions actions_;
  QString pendingProblemPath_;
  QString pendingModelPath_;
  QStringList recentProblems_;
  bool initialModelChoicePending_ = true;
  bool firstWorkspaceShown_ = false;
  QElapsedTimer documentLoadTimer_;
  QElapsedTimer modelLoadTimer_;
  QElapsedTimer firstWorkspaceDisplayTimer_;
};

#endif
