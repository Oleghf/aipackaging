#ifndef MAINSCENEWIDGET_H__
#define MAINSCENEWIDGET_H__

#include <QWidget>

////////////////////////////////////////////////////////////////////////////////
//
/// Виджет сцены
/**
*/
////////////////////////////////////////////////////////////////////////////////
class SceneWidget : public QWidget
{
  Q_OBJECT
public:
  SceneWidget(QWidget * parent = nullptr);

  void mousePressEvent(QMouseEvent * event) override;
  void mouseMoveEvent(QMouseEvent * event) override;
  void mouseReleaseEvent(QMouseEvent * event) override;
  void wheelEvent(QWheelEvent * event) override;
  void paintEvent(QPaintEvent * event) override;

signals:
  // Сигнал с объектом рисования виджета
  void sceneQPainterCreated(QPainter & painter);
  // Сигнал с ивентом мыши виджета
  void sceneMouseEventGenerated(QMouseEvent * event);
  void sceneWheelEventGenerated(QWheelEvent * event);
};


#endif
