#ifndef AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H
#define AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H

#include <cstdint>
#include <QByteArray>
#include <QWidget>

#include <polygonworkspaceview.h>

class QAction;
class PolygonCanvasWidget;
class PolygonDocumentPanel;
class PolygonEditorPanel;
class PolygonEditorToolBar;
class PolygonProblemsPanel;
class PolygonRunPanel;
class PolygonStatusPanel;
class QSplitter;
class QTabWidget;

/// Совместимый фасад рабочей области, объединяющий документ, полотно, запуск и состояние.
class PolygonWorkspaceWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт профильные панели, полотно и прежнюю компоновку рабочей области.
  explicit PolygonWorkspaceWidget(QWidget * parent = nullptr);
  /// Возвращает проверяемые числовые настройки, выбранные пользователем.
  NestingRunRequest solverConfig() const;
  /// Сообщает, можно ли безопасно сформировать запрос из текущих полей.
  bool settingsValid() const;
  /// Передаёт согласованный снимок профильным панелям и полотну.
  void present(const PolygonWorkspaceSnapshot & snapshot);
  /// Передаёт полотну существующие действия редактора, не создавая отдельного прикладного порта.
  void setEditorActions(PolygonWorkspaceActions actions);
  /// Отменяет незавершённый жест перед запуском или заменой документа.
  void cancelEditorInteraction();
  /// Возвращает команду запуска для размещения на панели главного окна.
  QAction * startAction() const;
  /// Возвращает команду отмены для размещения на панели главного окна.
  QAction * cancelAction() const;
  /// Возвращает команду вписывания листа для размещения на панели главного окна.
  QAction * fitAction() const;
  /// Выбирает рекомендуемый гибридный режим.
  void selectRecommendedMode();
  /// Выбирает сохранённый пользовательский режим по стабильному индексу.
  void selectPreset(int preset);
  /// Возвращает индекс текущего пользовательского режима.
  int selectedPreset() const;
  /// Управляет раскрытием экспертных настроек.
  void setAdvancedExpanded(bool expanded);
  /// Сообщает, раскрыты ли экспертные настройки.
  bool advancedExpanded() const;
  /// Возвращает положение внутренних панелей.
  QByteArray splitterState() const;
  /// Восстанавливает положение внутренних панелей.
  void restoreSplitterState(const QByteArray & state);

signals:
  /// Запрашивает выбор и открытие задачи `polygon_problem`.
  void requestOpenProblem();
  /// Запрашивает выбор пути и сохранение решения `polygon_solution`.
  void requestSaveSolution();
  /// Запрашивает выбор каталога внешнего комплекта модели.
  void requestOpenModel();
  /// Запрашивает отмену фоновой проверки комплекта модели.
  void requestCancelModelLoad();
  /// Запрашивает запуск с текущими настройками.
  void requestStart();
  /// Запрашивает согласованную отмену текущего запуска.
  void requestCancel();
  /// Передаёт координаты курсора главному окну.
  void cursorPositionChanged(double x, double y, bool inside);
  /// Передаёт атомарное изменение активного документа главному окну.
  void requestEditDocument(const aipackaging::editor::EditorCommandBatch & batch);
  /// Передаёт краткую диагностику интерактивного инструмента строке состояния окна.
  void editorInteractionMessage(const QString & message);

private:
  /// Согласует общий `Esc` и не перехватывает букву вписывания в текстовых полях.
  bool eventFilter(QObject * watched, QEvent * event) override;
  PolygonDocumentPanel * documentPanel_;
  PolygonCanvasWidget * canvas_;
  PolygonEditorToolBar * editorToolBar_;
  PolygonEditorPanel * editorPanel_;
  PolygonProblemsPanel * problemsPanel_;
  PolygonRunPanel * runPanel_;
  QTabWidget * rightTabs_;
  PolygonStatusPanel * statusPanel_;
  QSplitter * splitter_;
  std::uint64_t presentedRevision_ = static_cast<std::uint64_t>(-1);
};

#endif
