#ifndef AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H
#define AIPACKAGING_GUI_POLYGONWORKSPACEWIDGET_H

#include <QWidget>

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

/// Вкладка просмотра полигональной задачи и запуска базового поиска.
class PolygonWorkspaceWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт полотно, панель файлов, настройки решателя, ход выполнения и метрики.
  explicit PolygonWorkspaceWidget(QWidget * parent = nullptr);
  /// Возвращает проверяемые числовые настройки, выбранные пользователем.
  NestingRunRequest solverConfig() const;
  /// Применяет снимок модели представления и согласованно переключает доступность элементов управления.
  void present(const PolygonWorkspaceSnapshot & snapshot);

signals:
  /// Запрашивает выбор и открытие задачи `polygon_problem`.
  void requestOpenProblem();
  /// Запрашивает выбор пути и сохранение решения `polygon_solution`.
  void requestSaveSolution();
  /// Запрашивает запуск с текущими настройками.
  void requestStart();
  /// Запрашивает согласованную отмену текущего запуска.
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
