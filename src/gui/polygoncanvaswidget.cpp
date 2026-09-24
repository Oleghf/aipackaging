#include <algorithm>
#include <array>
#include <cmath>
#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <polygoncanvaswidget.h>

namespace
{
constexpr double VIEW_PADDING = 24.0;

/// Создаёт замкнутый `QPainterPath` из кольца модели представления.
void appendRing(QPainterPath & path, const std::vector<PolygonViewPoint> & ring)
{
  if (ring.empty())
    return;
  path.moveTo(ring.front().x, ring.front().y);
  for (std::size_t index = 1; index < ring.size(); ++index)
    path.lineTo(ring[index].x, ring[index].y);
  path.closeSubpath();
}

/// Возвращает стабильный контрастный цвет по индексу типа детали.
QColor partColor(std::size_t index)
{
  constexpr std::array<const char *, 10> COLORS = {"#5B8FF9", "#61DDAA", "#65789B", "#F6BD16", "#7262FD",
                                                   "#78D3F8", "#9661BC", "#F6903D", "#008685", "#F08BB4"};
  return QColor(COLORS[index % COLORS.size()]);
}
} // namespace

/// Настраивает фон, минимальный размер и обработку мыши.
PolygonCanvasWidget::PolygonCanvasWidget(QWidget * parent)
  : QWidget(parent)
{
  setMinimumSize(560, 420);
  setMouseTracking(true);
  setCursor(Qt::OpenHandCursor);
  setAttribute(Qt::WA_OpaquePaintEvent);
}

/// Копирует модель представления; геометрия решателя не вычисляется в GUI.
void PolygonCanvasWidget::setSnapshot(const PolygonWorkspaceSnapshot & snapshot)
{
  snapshot_ = snapshot;
  update();
}

/// Возвращает камеру к детерминированному автоматическому вписыванию листа.
void PolygonCanvasWidget::fitToView()
{
  zoom_ = 1.0;
  pan_ = {};
  update();
}

/// Применяет преобразование к оси Y, направленной вверх, и рисует кольца с чередующимся заполнением отверстий.
void PolygonCanvasWidget::paintEvent(QPaintEvent * event)
{
  QWidget::paintEvent(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), QColor("#F7FAFC"));

  const PolygonSceneView & scene = snapshot_.scene;
  if (scene.sheetWidth <= 0.0 || scene.sheetHeight <= 0.0)
  {
    painter.setPen(QColor("#718096"));
    painter.drawText(rect(), Qt::AlignCenter, tr("Откройте polygon_problem v1"));
    return;
  }

  const double availableWidth = std::max(1.0, width() - 2.0 * VIEW_PADDING);
  const double availableHeight = std::max(1.0, height() - 2.0 * VIEW_PADDING);
  const double fitScale = std::min(availableWidth / scene.sheetWidth, availableHeight / scene.sheetHeight);
  const double scale = fitScale * zoom_;

  // Центрируем лист, инвертируем экранную ось Y и затем применяем пользовательское смещение.
  painter.translate(width() / 2.0 + pan_.x(), height() / 2.0 + pan_.y());
  painter.scale(scale, -scale);
  painter.translate(-scene.sheetWidth / 2.0, -scene.sheetHeight / 2.0);

  QPen sheetPen(QColor("#334155"));
  sheetPen.setCosmetic(true);
  sheetPen.setWidthF(2.0);
  painter.setPen(sheetPen);
  painter.setBrush(Qt::white);
  painter.drawRect(QRectF(0.0, 0.0, scene.sheetWidth, scene.sheetHeight));

  if (snapshot_.state == PolygonWorkspaceState::Completed || snapshot_.state == PolygonWorkspaceState::Cancelled)
  {
    const double remnantX = scene.sheetMargin + scene.usedLength;
    const double remnantRight = scene.sheetWidth - scene.sheetMargin;
    if (remnantRight > remnantX)
    {
      painter.setPen(Qt::NoPen);
      painter.setBrush(QColor(72, 187, 120, 44));
      painter.drawRect(QRectF(remnantX, scene.sheetMargin, remnantRight - remnantX, scene.sheetHeight - 2.0 * scene.sheetMargin));
    }
  }

  if (scene.sheetMargin > 0.0)
  {
    QPen marginPen(QColor("#94A3B8"));
    marginPen.setCosmetic(true);
    marginPen.setStyle(Qt::DashLine);
    painter.setPen(marginPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(scene.sheetMargin, scene.sheetMargin, scene.sheetWidth - 2.0 * scene.sheetMargin,
                            scene.sheetHeight - 2.0 * scene.sheetMargin));
  }

  for (const PolygonPlacedPartView & part : scene.placements)
  {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    appendRing(path, part.outer);
    for (const auto & hole : part.holes)
      appendRing(path, hole);
    QColor fill = partColor(part.colorIndex);
    fill.setAlpha(185);
    QPen outline(fill.darker(155));
    outline.setCosmetic(true);
    const bool selected = part.partId == selectedPartId_ && part.instanceIndex == selectedInstanceIndex_;
    outline.setWidthF(selected ? 3.0 : 1.5);
    if (selected)
      outline.setColor(QColor("#0F172A"));
    painter.setPen(outline);
    painter.setBrush(fill);
    painter.drawPath(path);
  }
}

/// Запоминает начало перетаскивания только для левой кнопки доступного лишь для чтения полотна.
void PolygonCanvasWidget::mousePressEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton)
  {
    panning_ = true;
    dragged_ = false;
    lastMousePosition_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

/// Добавляет экранное смещение без изменения координат модели представления.
void PolygonCanvasWidget::mouseMoveEvent(QMouseEvent * event)
{
  if (panning_)
  {
    const QPointF delta = event->position() - lastMousePosition_;
    if (std::abs(delta.x()) + std::abs(delta.y()) > 1.0)
      dragged_ = true;
    pan_ += delta;
    lastMousePosition_ = event->position();
    update();
    event->accept();
    return;
  }
  const QPointF sheet = mapToSheet(event->position());
  const bool inside =
    sheet.x() >= 0.0 && sheet.y() >= 0.0 && sheet.x() <= snapshot_.scene.sheetWidth && sheet.y() <= snapshot_.scene.sheetHeight;
  emit cursorPositionChanged(sheet.x(), sheet.y(), inside);
  QWidget::mouseMoveEvent(event);
}

/// Завершает перетаскивание и восстанавливает курсор открытой ладони.
void PolygonCanvasWidget::mouseReleaseEvent(QMouseEvent * event)
{
  if (event->button() == Qt::LeftButton && panning_)
  {
    panning_ = false;
    setCursor(Qt::OpenHandCursor);
    if (!dragged_)
    {
      if (const PolygonPlacedPartView * part = hitTest(mapToSheet(event->position())))
      {
        selectedPartId_ = part->partId;
        selectedInstanceIndex_ = part->instanceIndex;
        emit partSelected(QString::fromStdString(part->partId), part->instanceIndex);
        update();
      }
    }
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

/// Умножает масштаб на ограниченный экспоненциальный коэффициент прокрутки колеса.
void PolygonCanvasWidget::wheelEvent(QWheelEvent * event)
{
  const double factor = std::pow(1.0015, event->angleDelta().y());
  zoom_ = std::clamp(zoom_ * factor, 0.2, 20.0);
  update();
  event->accept();
}

/// Обращает преобразование камеры без изменения геометрии модели представления.
QPointF PolygonCanvasWidget::mapToSheet(const QPointF & position) const
{
  const PolygonSceneView & scene = snapshot_.scene;
  if (scene.sheetWidth <= 0.0 || scene.sheetHeight <= 0.0)
    return {};
  const double availableWidth = std::max(1.0, width() - 2.0 * VIEW_PADDING);
  const double availableHeight = std::max(1.0, height() - 2.0 * VIEW_PADDING);
  const double scale = std::min(availableWidth / scene.sheetWidth, availableHeight / scene.sheetHeight) * zoom_;
  return {(position.x() - width() / 2.0 - pan_.x()) / scale + scene.sheetWidth / 2.0,
          -(position.y() - height() / 2.0 - pan_.y()) / scale + scene.sheetHeight / 2.0};
}

/// Проверяет детали в обратном порядке отрисовки и учитывает прозрачные отверстия.
const PolygonPlacedPartView * PolygonCanvasWidget::hitTest(const QPointF & sheetPoint) const
{
  for (auto iterator = snapshot_.scene.placements.rbegin(); iterator != snapshot_.scene.placements.rend(); ++iterator)
  {
    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    appendRing(path, iterator->outer);
    for (const auto & hole : iterator->holes)
      appendRing(path, hole);
    if (path.contains(sheetPoint))
      return &*iterator;
  }
  return nullptr;
}
