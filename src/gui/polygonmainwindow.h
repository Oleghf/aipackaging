#ifndef AIPACKAGING_GUI_POLYGONMAINWINDOW_H
#define AIPACKAGING_GUI_POLYGONMAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>
#include <QVariantMap>

#include <polygonimportcontracts.h>
#include <polygonworkspaceview.h>

class PolygonWorkspaceWidget;
class QAction;
class QCloseEvent;
class QLabel;
class QStackedWidget;
class QTimer;
class PolygonStartPage;
class PolygonDxfImportWizard;

/// Показывает только полигональный раскрой и передаёт действия прикладному контроллеру.
class PolygonMainWindow final : public QMainWindow,
                                public IPolygonWorkspaceOutput,
                                public IPolygonImportOutput
{
  Q_OBJECT
public:
  /// Создаёт полигональную рабочую область без унаследованного клеточного редактора.
  explicit PolygonMainWindow(QWidget * parent = nullptr);

  /// Подключает действия контроллера и восстанавливает ранее проверенный путь модели.
  void setPolygonWorkspaceActions(PolygonWorkspaceActions actions);
  /// Подключает отдельные действия фонового импорта DXF.
  void setPolygonImportActions(PolygonImportActions actions);

  /// Отображает согласованный снимок состояния полигональной рабочей области.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & snapshot) override;
  /// Передаёт согласованный снимок открытому мастеру импорта.
  void presentPolygonImport(const PolygonImportSnapshot & snapshot) override;

protected:
  /// Сохраняет геометрию и расположение панелей перед закрытием окна.
  void closeEvent(QCloseEvent * event) override;

private:
  /// Открывает автономное руководство из каталога поставки, не обращаясь к сети.
  void showHelp();
  /// Показывает версию сборки и сведения о подключённой модели.
  void showAbout();
  /// Подключает комплектную модель относительно текущего расположения приложения.
  void useBundledModel();
  /// Собирает страницы, основные действия, панель инструментов и строку состояния.
  void buildWindow();
  /// Связывает команды окна, стартовой страницы и рабочей области.
  void connectActions();
  /// Открывает диалог сохранения и передаёт выбранный путь прикладному действию.
  void saveSolution();
  /// Сохраняет исходный документ в его формате либо предлагает путь для черновика.
  void saveDocument();
  /// Сохраняет точно корректный документ как задачу по выбранному новому пути.
  void saveProblemAs();
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
  /// Запрашивает обязательные поля и создаёт новый пустой документ.
  void createDocument();
  /// Выбирает файл DXF и открывает пошаговый мастер импорта.
  void chooseDxf();
  /// Повторно анализирует указанный DXF в новом мастере без автоматического принятия результата.
  void openDxf(const QString & path);
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
  QAction * createDocumentAction_;
  QAction * saveSolutionAction_;
  QAction * saveDocumentAction_;
  QAction * saveProblemAsAction_;
  QAction * openModelAction_;
  QAction * bundledModelAction_ = nullptr;
  QAction * importDxfAction_;
  QAction * forgetModelAction_;
  QAction * undoAction_;
  QAction * redoAction_;
  PolygonWorkspaceActions actions_;
  PolygonImportActions importActions_;
  PolygonDxfImportWizard * importWizard_;
  PolygonWorkspaceSnapshot lastSnapshot_;
  QTimer * autosaveTimer_;
  QString pendingProblemPath_;
  QString pendingModelPath_;
  bool pendingBundledModel_ = false;
  int initialModelPreset_ = 0;
  QStringList recentProblems_;
  QVariantMap recentSourceKinds_;
  bool initialModelChoicePending_ = true;
  bool firstWorkspaceShown_ = false;
  QElapsedTimer documentLoadTimer_;
  QElapsedTimer modelLoadTimer_;
  QElapsedTimer firstWorkspaceDisplayTimer_;
};

#endif
