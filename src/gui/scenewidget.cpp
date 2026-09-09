#include <QMouseEvent>
#include <QPainter>

#include <scenewidget.h>


//------------------------------------------------------------------------------
/**
  Конструктор
*/
//--
SceneWidget::SceneWidget(QWidget * parent)
  : QWidget(parent)
{
  setAttribute(Qt::WA_StyledBackground, true);
}


//------------------------------------------------------------------------------
/**
  Обработка события нажатия кнопок мыши
*/
//--
void SceneWidget::mousePressEvent(QMouseEvent * event)
{
  emit sceneMouseEventGenerated(event);
}


//------------------------------------------------------------------------------
/**
  Обработка события движения мыши
*/
//--
void SceneWidget::mouseMoveEvent(QMouseEvent * event)
{
  emit sceneMouseEventGenerated(event);
}


//------------------------------------------------------------------------------
/**
  Обработка события отпускания кнопок мыши
*/
//--
void SceneWidget::mouseReleaseEvent(QMouseEvent * event)
{
  emit sceneMouseEventGenerated(event);
}


//
void SceneWidget::wheelEvent(QWheelEvent * event)
{
  emit sceneWheelEventGenerated(event);
}


//------------------------------------------------------------------------------
/**
  Обработка события рисования
*/
//--
void SceneWidget::paintEvent(QPaintEvent * event)
{
  QWidget::paintEvent(event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  emit sceneQPainterCreated(painter);
  painter.resetTransform();
  painter.setPen({Qt::lightGray, 2});
  painter.drawRect(rect());
}
