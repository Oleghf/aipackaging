#ifndef AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H
#define AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H

#include <cstdint>
#include <QByteArray>
#include <QWidget>

#include <polygonworkspaceview.h>

class QComboBox;
class QAction;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QSplitter;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QToolButton;
class PolygonCanvasWidget;

/// Вкладка просмотра полигональной задачи и запуска базового поиска.
class PolygonWorkspaceWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт полотно, панель файлов, настройки решателя, ход выполнения и метрики.
  explicit PolygonWorkspaceWidget(QWidget * parent = nullptr);
  /// Возвращает проверяемые числовые настройки, выбранные пользователем.
  NestingRunRequest solverConfig() const;
  /// Сообщает, можно ли безопасно сформировать запрос из текущих полей.
  bool settingsValid() const;
  /// Применяет снимок модели представления и согласованно переключает доступность элементов управления.
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
  /// Пересчитывает доступность запуска по снимку, модели и корректности настроек.
  void updateStartAvailability();
  /// Применяет значения выбранного понятного режима к запросу запуска.
  void applyPreset(int preset);
  /// Показывает сведения о выбранном экземпляре детали.
  void showSelectedPart(const QString & partId, std::uint32_t instanceIndex);

  PolygonCanvasWidget * canvas_;
  QPushButton * openButton_;
  QPushButton * saveButton_;
  QPushButton * startButton_;
  QPushButton * cancelButton_;
  QPushButton * fitButton_;
  QPushButton * modelButton_;
  QPushButton * cancelModelButton_;
  QComboBox * solverBox_;
  QComboBox * expertSolverBox_;
  QToolButton * advancedToggle_;
  QGroupBox * advancedGroup_;
  QLineEdit * seedEdit_;
  QLabel * seedValidationLabel_;
  QSpinBox * randomIterationsSpin_;
  QSpinBox * beamWidthSpin_;
  QSpinBox * maxExpandedSpin_;
  QSpinBox * timeoutSpin_;
  QSpinBox * neuralRolloutsSpin_;
  QLabel * problemLabel_;
  QLabel * modelLabel_;
  QLabel * statusLabel_;
  QProgressBar * progressBar_;
  QTextEdit * metricsText_;
  QListWidget * unplacedList_;
  QTreeWidget * partTree_;
  QLabel * partDetailsLabel_;
  QLabel * stageLabel_;
  QSplitter * splitter_;
  QAction * startAction_;
  QAction * cancelAction_;
  QAction * fitAction_;
  PolygonWorkspaceSnapshot snapshot_;
  bool applyingPreset_ = false;
};

#endif
