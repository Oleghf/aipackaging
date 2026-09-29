#ifndef AIPACKAGING_GUI_POLYGONDXFIMPORTWIZARD_H
#define AIPACKAGING_GUI_POLYGONDXFIMPORTWIZARD_H

#include <QWizard>

#include <polygonimportcontracts.h>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class DxfPathPreview;

/// Проводит технолога через проверяемые этапы импорта ASCII DXF.
class PolygonDxfImportWizard final : public QWizard
{
  Q_OBJECT
public:
  /// Создаёт пять страниц мастера без запуска файловых операций.
  explicit PolygonDxfImportWizard(QWidget * parent = nullptr);
  /// Сохраняет путь и прикладные действия, затем начинает фоновый анализ.
  void startImport(QString filePath, PolygonImportActions actions);
  /// Публикует состояние фонового сценария и обновляет доступность мастера.
  void present(const PolygonImportSnapshot & snapshot);

protected:
  /// На последней странице сначала строит документ, а после проверки принимает его.
  void accept() override;
  /// Отменяет работу и освобождает сеанс перед закрытием мастера.
  void reject() override;

private:
  /// Создаёт страницы выбора единиц, слоёв, цепочек, деталей и параметров задачи.
  void buildPages();
  /// Повторно анализирует файл с выбранной единицей, допуском и соединениями.
  void reanalyze();
  /// Заполняет элементы управления результатом нового анализа.
  void populateInspection(const PolygonImportInspection & inspection);
  /// Перестраивает список типов деталей по назначениям цепочек.
  void rebuildParts();
  /// Собирает подтверждённые значения всех страниц в прикладной запрос.
  PolygonImportConfiguration configuration() const;
  /// Возвращает выбранную единицу длины из данных элемента списка.
  PolygonImportUnit selectedUnit() const;

  QString filePath_;
  PolygonImportActions actions_;
  PolygonImportSnapshot snapshot_;
  std::vector<std::uint64_t> appliedJoins_;
  QLabel * statusLabel_;
  QComboBox * unitCombo_;
  QDoubleSpinBox * joinTolerance_;
  QPushButton * reanalyzeButton_;
  QListWidget * layersList_;
  QCheckBox * ignoreUnsupported_;
  QListWidget * joinsList_;
  QTableWidget * assignmentsTable_;
  DxfPathPreview * preview_;
  QTableWidget * partsTable_;
  QCheckBox * groupDuplicates_;
  QLineEdit * problemId_;
  QDoubleSpinBox * sheetWidth_;
  QDoubleSpinBox * sheetHeight_;
  QDoubleSpinBox * sheetMargin_;
  QDoubleSpinBox * partSpacing_;
  QDoubleSpinBox * kerf_;
  QDoubleSpinBox * curveTolerance_;
  QLabel * resultLabel_;
  bool inspectionPopulated_ = false;
};

#endif
