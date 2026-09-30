#ifndef AIPACKAGING_GUI_POLYGONEDITORTOOLBAR_H
#define AIPACKAGING_GUI_POLYGONEDITORTOOLBAR_H

#include <QWidget>

#include <polygoneditorcanvasinteraction.h>

class QAction;
class QCheckBox;
class QDoubleSpinBox;

/// Предоставляет доступные инструменты полотна и явные настройки привязок.
class PolygonEditorToolBar final : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт взаимоисключающие инструменты и восстанавливает постоянные настройки сетки.
  explicit PolygonEditorToolBar(QWidget * parent = nullptr);
  /// Возвращает выбранный инструмент рисования или выбора.
  PolygonCanvasTool tool() const noexcept;
  /// Возвращает согласованные параметры сетки и привязок.
  PolygonCanvasSnapSettings snapSettings() const;
  /// Возвращает выбранное направление создаваемой дуги.
  bool arcClockwise() const noexcept;
  /// Принудительно возвращает инструмент выбора без изменения постоянных настроек.
  void selectTool(PolygonCanvasTool tool);
  /// Управляет доступностью редактирования при публикации снимка приложения.
  void setEditingEnabled(bool enabled);

signals:
  /// Сообщает полотну о смене активного инструмента.
  void toolChanged(PolygonCanvasTool tool);
  /// Сообщает полотну о смене сетки или привязок.
  void snapSettingsChanged(const PolygonCanvasSnapSettings & settings);
  /// Сообщает полотну о смене направления дуги.
  void arcDirectionChanged(bool clockwise);

private:
  /// Сохраняет пользовательские настройки редактора и публикует их одним событием.
  void publishSettings();

  QAction * selectAction_;
  QAction * outerAction_;
  QAction * holeAction_;
  QAction * lineAction_;
  QAction * arcAction_;
  QAction * bezierAction_;
  QCheckBox * gridVisible_;
  QDoubleSpinBox * gridStep_;
  QCheckBox * geometrySnap_;
  QCheckBox * gridSnap_;
  QCheckBox * clockwise_;
  PolygonCanvasTool tool_ = PolygonCanvasTool::Select;
};

#endif
