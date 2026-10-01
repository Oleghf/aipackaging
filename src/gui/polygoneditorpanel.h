#ifndef AIPACKAGING_GUI_POLYGONEDITORPANEL_H
#define AIPACKAGING_GUI_POLYGONEDITORPANEL_H

#include <cstdint>
#include <memory>
#include <QWidget>
#include <string>
#include <vector>

#include <polygonworkspaceview.h>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

/// Предоставляет числовые формы команд полигонального редактора без прямого изменения модели.
class PolygonEditorPanel final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт формы документа, деталей и геометрии с доступной клавиатурной навигацией.
  explicit PolygonEditorPanel(QWidget * parent = nullptr);
  /// Публикует неизменяемый снимок документа и согласует доступность команд.
  void present(const PolygonWorkspaceSnapshot & snapshot);
  /// Выбирает существующие устойчивые сущности без изменения документа.
  void selectEntities(const std::vector<std::uint64_t> & entityIds);

signals:
  /// Передаёт атомарный пакет прикладному контроллеру документа.
  void editRequested(const aipackaging::editor::EditorCommandBatch & batch);
  /// Сообщает полотну выбранную устойчивую сущность без изменения прикладного документа.
  void entitySelected(std::uint64_t entityId);
  /// Сообщает полотну и панели проблем полный устойчивый набор выбранных сущностей.
  void entitiesSelected(const std::vector<std::uint64_t> & entityIds);

private:
  /// Создаёт элементы форм и связывает их с командами редактора.
  void buildUi();
  /// Перестраивает дерево, сохраняя выбранный устойчивый идентификатор.
  void rebuildTree();
  /// Публикует свойства выбранной сущности в числовых полях.
  void presentSelection();
  /// Возвращает выбранный идентификатор требуемого вида либо нулевое значение.
  aipackaging::editor::EntityId selectedEntity(int kind) const;
  /// Возвращает тип детали, которому принадлежит выбранная сущность.
  aipackaging::editor::EntityId selectedPart() const;
  /// Возвращает цепочку, которой принадлежит выбранная сущность.
  aipackaging::editor::EntityId selectedPath() const;
  /// Разбирает список целых углов, разделённых запятыми.
  std::vector<int> rotations() const;
  /// Формирует параметры сегмента из полей выбранного вида.
  aipackaging::editor::EditorSegmentValue segmentValue() const;
  /// Отправляет один подписанный пакет, если редактирование доступно.
  void submit(std::string label, std::vector<aipackaging::editor::EditorCommand> commands);

  PolygonWorkspaceSnapshot snapshot_;
  std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> treeDocument_;
  QTreeWidget * tree_;
  QLineEdit * problemId_;
  QDoubleSpinBox * sheetWidth_;
  QDoubleSpinBox * sheetHeight_;
  QDoubleSpinBox * sheetMargin_;
  QDoubleSpinBox * spacing_;
  QDoubleSpinBox * kerf_;
  QDoubleSpinBox * tolerance_;
  QLineEdit * partId_;
  QSpinBox * quantity_;
  QLineEdit * rotations_;
  QComboBox * segmentKind_;
  QCheckBox * clockwise_;
  QDoubleSpinBox * pointX_;
  QDoubleSpinBox * pointY_;
  QDoubleSpinBox * auxiliary1X_;
  QDoubleSpinBox * auxiliary1Y_;
  QDoubleSpinBox * auxiliary2X_;
  QDoubleSpinBox * auxiliary2Y_;
  QListWidget * diagnostics_;
  QWidget * commandArea_;
  bool updating_ = false;
};

#endif
