#ifndef AIPACKAGING_GUI_POLYGONPROBLEMSPANEL_H
#define AIPACKAGING_GUI_POLYGONPROBLEMSPANEL_H

#include <cstdint>
#include <QWidget>
#include <vector>

#include <polygonworkspaceview.h>

class QListWidget;
class QPushButton;

/// Показывает структурированные проблемы документа и только подтверждаемые исправления.
class PolygonProblemsPanel final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт доступный список диагностик и команду явного исправления.
  explicit PolygonProblemsPanel(QWidget * parent = nullptr);
  /// Публикует диагностики текущей редакции и сохраняет возможный выбор.
  void present(const PolygonWorkspaceSnapshot & snapshot);
  /// Согласует выбор с полотном и деревом редактора.
  void selectEntities(const std::vector<std::uint64_t> & entities);

signals:
  /// Передаёт связанные сущности выбранной диагностики.
  void entitiesSelected(const std::vector<std::uint64_t> & entities);
  /// Передаёт подтверждённое однозначное исправление прикладному контроллеру.
  void fixRequested(const aipackaging::editor::EditorCommandBatch & batch);

private:
  /// Публикует сущности и доступность исправления для текущей строки.
  void presentSelection();
  /// Показывает состав изменения и отправляет его только после подтверждения.
  void applySelectedFix();

  PolygonWorkspaceSnapshot snapshot_;
  QListWidget * list_;
  QPushButton * fixButton_;
  bool updating_ = false;
};

#endif
