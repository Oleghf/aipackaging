#ifndef AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H
#define AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H

#include <QByteArray>
#include <QWidget>

#include <polygonworkspaceview.h>

class QAction;
class PolygonCanvasWidget;
class PolygonDocumentPanel;
class PolygonRunPanel;
class PolygonStatusPanel;
class QSplitter;

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

private:
  PolygonDocumentPanel * documentPanel_;
  PolygonCanvasWidget * canvas_;
  PolygonRunPanel * runPanel_;
  PolygonStatusPanel * statusPanel_;
  QSplitter * splitter_;
};

#endif
