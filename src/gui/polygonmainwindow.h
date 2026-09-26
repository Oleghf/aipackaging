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
class QTimer;
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
  /// Открывает диалог сохранения и передаёт выбранный путь прикладному действию.
  void saveSolution();
  /// Сохраняет исходный документ в его формате либо предлагает путь для черновика.
  void saveDocument();
  /// Открывает диалог каталога и запускает фоновую проверку модели.
  void chooseModel();
  /// Освобождает выбранную модель и удаляет сохранённый путь.
  void forgetModel();
  /// Удаляет путь из списка недавних задач и обновляет стартовую страницу.
  void removeRecentProblem(const QString & path);
  /// Формирует запрос из рабочей области и запускает раскрой.
  void startRun();
  /// Передаёт запрос отмены текущего раскроя.
  void cancelRun();
  /// Передаёт запрос отмены фоновой проверки модели.
  void cancelModelLoad();
  /// Показывает координаты курсора только внутри листа.
  void showCursorPosition(double x, double y, bool inside);
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
  /// Предлагает сохранить, отбросить либо отменить замену грязного документа.
  bool confirmDocumentReplacement();

  PolygonWorkspaceWidget * workspace_;
  PolygonStartPage * startPage_;
  QStackedWidget * pages_;
  QLabel * coordinatesLabel_;
  QAction * homeAction_;
  QAction * openProblemAction_;
  QAction * saveSolutionAction_;
  QAction * saveDocumentAction_;
  QAction * openModelAction_;
  QAction * forgetModelAction_;
  PolygonWorkspaceActions actions_;
  PolygonWorkspaceSnapshot lastSnapshot_;
  QTimer * autosaveTimer_;
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
