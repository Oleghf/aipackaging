#ifndef AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H
#define AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H

#include <QWidget>

#include <gridtypes.h>
#include <polygonworkspaceview.h>

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QToolButton;
class PolygonCanvasWidget;

/// Вкладка управления read-only полигональной задачей и baseline-поиском.
class PolygonWorkspaceWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт полотно, панель файлов, solver-настроек, прогресса и метрик.
  explicit PolygonWorkspaceWidget(QWidget * parent = nullptr);
  /// Возвращает проверяемые числовые настройки, выбранные пользователем.
  aipackaging::solver::SolverConfig solverConfig() const;
  /// Применяет presentation-снимок и согласованно переключает доступность контролов.
  void present(const PolygonWorkspaceSnapshot & snapshot);

signals:
  /// Запрашивает выбор и открытие polygon_problem.
  void requestOpenProblem();
  /// Запрашивает выбор пути и сохранение polygon_solution.
  void requestSaveSolution();
  /// Запрашивает запуск с текущими настройками.
  void requestStart();
  /// Запрашивает cooperative cancellation текущего запуска.
  void requestCancel();

private:
  PolygonCanvasWidget * canvas_;
  QPushButton * openButton_;
  QPushButton * saveButton_;
  QPushButton * startButton_;
  QPushButton * cancelButton_;
  QPushButton * fitButton_;
  QComboBox * solverBox_;
  QToolButton * advancedToggle_;
  QGroupBox * advancedGroup_;
  QLineEdit * seedEdit_;
  QSpinBox * randomIterationsSpin_;
  QSpinBox * beamWidthSpin_;
  QSpinBox * maxExpandedSpin_;
  QSpinBox * timeoutSpin_;
  QLabel * problemLabel_;
  QLabel * statusLabel_;
  QProgressBar * progressBar_;
  QTextEdit * metricsText_;
  QListWidget * unplacedList_;
};

#endif
