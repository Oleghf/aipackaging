#include <QColor>
#include <QPainter>

#include <polygoncanvasrenderer.h>
#include <polygoncanvaswidget.h>

/// Выбирает режим исходной задачи или результата и сохраняет установленный порядок слоёв.
void PolygonCanvasRenderer::paint(PolygonCanvasWidget & canvas)
{
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(canvas.rect(), QColor("#F7FAFC"));
  const PolygonSceneView & scene = canvas.snapshot_.scene;
  if (scene.sheetWidth <= 0.0 || scene.sheetHeight <= 0.0)
  {
    painter.setPen(QColor("#718096"));
    painter.drawText(canvas.rect(), Qt::AlignCenter, PolygonCanvasWidget::tr("Откройте polygon_problem v1 или создайте задачу"));
    return;
  }
  canvas.applySceneTransform(painter, scene);
  canvas.drawSheet(painter, scene);
  if (canvas.mode_ == PolygonCanvasMode::Source)
    canvas.drawGrid(painter, scene);
  canvas.drawMargin(painter, scene);
  if (canvas.mode_ == PolygonCanvasMode::Source)
    canvas.drawEditableDocument(painter);
  else
  {
    canvas.drawRemnant(painter, scene);
    canvas.drawPlacements(painter, scene);
  }
  canvas.drawInteractionOverlay(painter);
}
