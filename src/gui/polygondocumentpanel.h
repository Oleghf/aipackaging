#ifndef AIPACKAGING_GUI_POLYGONDOCUMENTPANEL_H
#define AIPACKAGING_GUI_POLYGONDOCUMENTPANEL_H

#include <cstdint>
#include <QString>
#include <QWidget>

#include <polygonworkspaceview.h>

class QLabel;
class QTreeWidget;

/// Показывает сведения о задаче, составе деталей, легенде и выбранном экземпляре.
class PolygonDocumentPanel final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт панель документа с деревом деталей и неизменяемой легендой.
  explicit PolygonDocumentPanel(QWidget * parent = nullptr);

  /// Публикует сведения документа и пересобирает дерево типов деталей.
  void present(const PolygonWorkspaceSnapshot & snapshot);
  /// Показывает свойства указанного экземпляра, если его тип присутствует в документе.
  void showSelectedPart(const QString & partId, std::uint32_t instanceIndex);

signals:
  /// Сообщает о выборе типа детали в дереве документа.
  void partSelected(const QString & partId, std::uint32_t instanceIndex);

private:
  QLabel * problemLabel_;
  QTreeWidget * partTree_;
  QLabel * partDetailsLabel_;
  PolygonWorkspaceSnapshot snapshot_;
};

#endif
