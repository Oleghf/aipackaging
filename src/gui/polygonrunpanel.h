#ifndef AIPACKAGING_GUI_POLYGONRUNPANEL_H
#define AIPACKAGING_GUI_POLYGONRUNPANEL_H

#include <QWidget>

#include <polygonworkspaceview.h>

class QAction;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;

/// Управляет выбором режима, моделью, параметрами запуска и неразмещёнными экземплярами.
class PolygonRunPanel final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт правую панель с прежними значениями параметров и именами объектов Qt.
  explicit PolygonRunPanel(QWidget * parent = nullptr);

  /// Возвращает проверяемый прикладной запрос, сформированный из элементов управления.
  NestingRunRequest solverConfig() const;
  /// Сообщает, можно ли безопасно преобразовать поля панели в запрос.
  bool settingsValid() const;
  /// Публикует состояние модели, работы и список неразмещённых экземпляров.
  void present(const PolygonWorkspaceSnapshot & snapshot);
  /// Возвращает команду запуска с общим сочетанием клавиш.
  QAction * startAction() const;
  /// Возвращает команду отмены с общим сочетанием клавиш.
  QAction * cancelAction() const;
  /// Возвращает команду вписывания листа с общим сочетанием клавиш.
  QAction * fitAction() const;
  /// Выбирает рекомендуемый гибридный режим.
  void selectRecommendedMode();
  /// Выбирает сохранённый понятный режим по стабильному индексу.
  void selectPreset(int preset);
  /// Возвращает индекс понятного режима либо быстрый режим для пользовательских настроек.
  int selectedPreset() const;
  /// Управляет раскрытием экспертных параметров.
  void setAdvancedExpanded(bool expanded);
  /// Сообщает, раскрыты ли экспертные параметры.
  bool advancedExpanded() const;

signals:
  /// Запрашивает открытие задачи через совместимый фасад.
  void requestOpenProblem();
  /// Запрашивает сохранение решения через совместимый фасад.
  void requestSaveSolution();
  /// Запрашивает выбор комплекта модели.
  void requestOpenModel();
  /// Запрашивает отмену проверки модели.
  void requestCancelModelLoad();
  /// Запрашивает запуск с текущими настройками.
  void requestStart();
  /// Запрашивает отмену текущего запуска.
  void requestCancel();
  /// Запрашивает вписывание листа в полотно.
  void requestFit();

private:
  /// Пересчитывает доступность запуска и показывает ошибку начального значения.
  void updateStartAvailability();
  /// Заполняет экспертные параметры значениями выбранного понятного режима.
  void applyPreset(int preset);

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
  QLabel * modelLabel_;
  QListWidget * unplacedList_;
  QAction * startAction_;
  QAction * cancelAction_;
  QAction * fitAction_;
  bool applyingPreset_ = false;
};

#endif
