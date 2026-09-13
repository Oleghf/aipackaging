#ifndef AIPACKAGING_GUI_POLYGONCANVASWIDGET_H
#define AIPACKAGING_GUI_POLYGONCANVASWIDGET_H

#include <QPointF>
#include <QWidget>

#include <polygonworkspaceview.h>

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

/// Виджет доступной только для чтения визуализации полигонального листа и проверенной раскладки.
class PolygonCanvasWidget : public QWidget
{
  Q_OBJECT
public:
  /// Создаёт полотно с автоматическим вписыванием листа.
  explicit PolygonCanvasWidget(QWidget * parent = nullptr);
  /// Заменяет отображаемый снимок и запрашивает перерисовку.
  void setSnapshot(const PolygonWorkspaceSnapshot & snapshot);

  /// Сбрасывает пользовательский масштаб и смещение, чтобы вписать сцену в область просмотра.
  void fitToView();

protected:
  /// Рисует лист, отступ, полезный остаток, детали и отверстия.
  void paintEvent(QPaintEvent * event) override;
  /// Начинает перемещение камеры левой кнопкой мыши.
  void mousePressEvent(QMouseEvent * event) override;
  /// Обновляет смещение камеры во время перетаскивания.
  void mouseMoveEvent(QMouseEvent * event) override;
  /// Завершает перемещение камеры.
  void mouseReleaseEvent(QMouseEvent * event) override;
  /// Масштабирует сцену колесом относительно центра полотна.
  void wheelEvent(QWheelEvent * event) override;

private:
  PolygonWorkspaceSnapshot snapshot_;
  double zoom_ = 1.0;
  QPointF pan_;
  QPointF lastMousePosition_;
  bool panning_ = false;
};

#endif
