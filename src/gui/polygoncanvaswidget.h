#ifndef AIPACKAGING_GUI_POLYGONCANVASWIDGET_H
#define AIPACKAGING_GUI_POLYGONCANVASWIDGET_H

#include <cstdint>
#include <QPointF>
#include <QString>
#include <QWidget>
#include <string>

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

signals:
  /// Сообщает координаты курсора в миллиметрах и нахождение внутри листа.
  void cursorPositionChanged(double x, double y, bool inside);
  /// Сообщает выбранный щелчком экземпляр детали.
  void partSelected(const QString & partId, std::uint32_t instanceIndex);

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
  /// Переводит экранную точку в миллиметровые координаты листа.
  QPointF mapToSheet(const QPointF & position) const;
  /// Находит верхнюю деталь под точкой с учётом отверстий.
  const PolygonPlacedPartView * hitTest(const QPointF & sheetPoint) const;

  PolygonWorkspaceSnapshot snapshot_;
  double zoom_ = 1.0;
  QPointF pan_;
  QPointF lastMousePosition_;
  bool panning_ = false;
  bool dragged_ = false;
  std::string selectedPartId_;
  std::uint32_t selectedInstanceIndex_ = 0;
};

#endif
