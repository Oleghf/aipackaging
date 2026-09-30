#include <algorithm>
#include <array>
#include <cmath>
#include <QColor>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QWheelEvent>
#include <unordered_set>

#include <aipackaging/editor/polygon_editor_interaction.h>
#include <polygoncanvaswidget.h>

using namespace aipackaging::editor;

namespace
{
constexpr double VIEW_PADDING = 24.0;
constexpr double HIT_TOLERANCE_PX = 8.0;
constexpr double SNAP_TOLERANCE_PX = 10.0;
constexpr qint64 DRAG_PUBLISH_INTERVAL_MS = 33;

/// Создаёт замкнутый путь Qt из кольца модели представления.
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

/// Сообщает, содержит ли устойчивый набор указанный идентификатор.
bool contains(const std::vector<EntityId> & values, EntityId value)
{
  return std::find(values.begin(), values.end(), value) != values.end();
}

/// Переносит выбранные точки только в локальной копии для плавного предварительного просмотра.
void translatePreview(EditablePolygonDocument & document, const std::vector<EntityId> & points, const QPointF & delta)
{
  const auto translate = [&](EditablePoint & point)
  {
    if (contains(points, point.id))
    {
      point.x += delta.x();
      point.y += delta.y();
    }
  };
  for (EditablePart & part : document.parts)
  {
    const auto translatePath = [&](EditablePath & path)
    {
      for (EditablePoint & point : path.vertices)
        translate(point);
      for (EditableSegment & segment : path.segments)
      {
        if (segment.kind == EditableSegmentKind::Arc)
          translate(segment.center);
        else if (segment.kind == EditableSegmentKind::CubicBezier)
        {
          translate(segment.control1);
          translate(segment.control2);
        }
      }
    };
    if (part.outer)
      translatePath(*part.outer);
    for (EditablePath & hole : part.holes)
      translatePath(hole);
  }
}

/// Находит тип детали по устойчивому идентификатору.
const EditablePart * findPart(const EditablePolygonDocument & document, EntityId id)
{
  const auto found =
    std::find_if(document.parts.begin(), document.parts.end(), [id](const EditablePart & part) { return part.id == id; });
  return found == document.parts.end() ? nullptr : &*found;
}

/// Находит цепочку в указанной детали или во всём документе.
const EditablePath * findPath(const EditablePolygonDocument & document, EntityId id)
{
  for (const EditablePart & part : document.parts)
  {
    if (part.outer && part.outer->id == id)
      return &*part.outer;
    const auto hole =
      std::find_if(part.holes.begin(), part.holes.end(), [id](const EditablePath & path) { return path.id == id; });
    if (hole != part.holes.end())
      return &*hole;
  }
  return nullptr;
}

/// Рисует направленный маркер постоянного экранного размера в середине сегмента.
void drawDirectionMarker(QPainter & painter, const QPainterPath & segment)
{
  const QPointF before = segment.pointAtPercent(0.45);
  const QPointF after = segment.pointAtPercent(0.55);
  const double length = std::hypot(after.x() - before.x(), after.y() - before.y());
  const double scale = std::abs(painter.worldTransform().m11());
  if (length <= 0.0 || scale <= 0.0)
    return;
  const double size = 6.0 / scale;
  const double directionX = (after.x() - before.x()) / length;
  const double directionY = (after.y() - before.y()) / length;
  const QPointF tip = segment.pointAtPercent(0.55);
  const QPointF base(tip.x() - directionX * size, tip.y() - directionY * size);
  const QPointF normal(-directionY * size * 0.45, directionX * size * 0.45);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor("#1E3A8A"));
  painter.drawPolygon(QPolygonF{tip, base + normal, base - normal});
  painter.restore();
}

/// Возвращает все идентификаторы диагностики для подсветки на полотне.
std::vector<EntityId> diagnosticEntities(const PolygonWorkspaceSnapshot & snapshot)
{
  std::vector<EntityId> result;
  std::unordered_set<std::uint64_t> seen;
  const auto append = [&](EntityId id)
  {
    if (id && seen.insert(id.value).second)
      result.push_back(id);
  };
  for (const DocumentDiagnostic & diagnostic : snapshot.documentDiagnostics)
  {
    if (diagnostic.severity != DiagnosticSeverity::Error)
      continue;
    append(diagnostic.entity);
    for (EntityId related : diagnostic.relatedEntities)
      append(related);
  }
  return result;
}
} // namespace

/// Настраивает фон, фокус, минимальный размер и постоянное отслеживание указателя.
PolygonCanvasWidget::PolygonCanvasWidget(QWidget * parent)
  : QWidget(parent)
{
  setMinimumSize(560, 420);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::ArrowCursor);
  setAttribute(Qt::WA_OpaquePaintEvent);
}

/// Обновляет документ и удаляет только те временные ссылки, которых больше нет в новой редакции.
void PolygonCanvasWidget::setSnapshot(const PolygonWorkspaceSnapshot & snapshot)
{
  const QString identity =
    QString::fromStdString(snapshot.problemId) + QLatin1Char('|') + QString::fromStdString(snapshot.document.sourceIdentifier);
  const bool replaced = !documentIdentity_.isEmpty() && identity != documentIdentity_;
  snapshot_ = snapshot;
  documentIdentity_ = identity;
  diagnosticEntities_ = diagnosticEntities(snapshot_);
  if (replaced || !snapshot_.editableDocument)
  {
    selectedEditorEntities_.clear();
    activePart_ = {};
    drawingPath_ = {};
    drawingPart_ = {};
    drawingStagePoints_.clear();
    provisionalPathGesture_.reset();
  }
  else
  {
    std::erase_if(selectedEditorEntities_,
                  [this](EntityId id) { return !findEditorEntity(*snapshot_.editableDocument, id).has_value(); });
    if (drawingPath_ && !findPath(*snapshot_.editableDocument, drawingPath_))
      drawingPath_ = {};
  }
  if (!snapshot_.canEdit && dragging_)
    finishDrag(true);
  update();
}

/// Копирует существующий набор функций, вызываемых только синхронно в потоке владельца окна.
void PolygonCanvasWidget::setEditorActions(PolygonWorkspaceActions actions)
{
  editorActions_ = std::move(actions);
}

/// Преобразует прежний одиночный выбор в общий устойчивый набор.
void PolygonCanvasWidget::selectEditorEntity(std::uint64_t entityId)
{
  selectEditorEntities(entityId == 0 ? std::vector<std::uint64_t>{} : std::vector<std::uint64_t>{entityId});
}

/// Сохраняет только существующие идентификаторы и выводит на передний план их тип детали.
void PolygonCanvasWidget::selectEditorEntities(const std::vector<std::uint64_t> & entityIds)
{
  if (!snapshot_.editableDocument)
    return;
  std::vector<EntityId> selection;
  for (std::uint64_t value : entityIds)
  {
    const EntityId id{value};
    const auto reference = findEditorEntity(*snapshot_.editableDocument, id);
    if (!reference || contains(selection, id))
      continue;
    selection.push_back(id);
    if (!activePart_)
      activePart_ = reference->part;
  }
  selectedEditorEntities_ = std::move(selection);
  if (!selectedEditorEntities_.empty())
  {
    const auto reference = findEditorEntity(*snapshot_.editableDocument, selectedEditorEntities_.front());
    if (reference)
      activePart_ = reference->part;
  }
  update();
}

/// Отменяет незавершённое изменение при уходе из режима исходной задачи.
void PolygonCanvasWidget::setCanvasMode(PolygonCanvasMode mode)
{
  if (mode_ == mode)
    return;
  if (mode == PolygonCanvasMode::Solution)
    cancelEditorInteraction();
  mode_ = mode;
  update();
}

/// Сбрасывает прежнюю стадию только при фактической смене инструмента.
void PolygonCanvasWidget::setEditorTool(PolygonCanvasTool tool)
{
  if (tool_ == tool)
    return;
  const bool segmentToSegment =
    provisionalPathGesture_ &&
    (tool_ == PolygonCanvasTool::Line || tool_ == PolygonCanvasTool::Arc || tool_ == PolygonCanvasTool::CubicBezier) &&
    (tool == PolygonCanvasTool::Line || tool == PolygonCanvasTool::Arc || tool == PolygonCanvasTool::CubicBezier);
  clearDrawingPreview(!segmentToSegment);
  if (dragging_)
    finishDrag(true);
  tool_ = tool;
  if (tool_ != PolygonCanvasTool::Select)
    mode_ = PolygonCanvasMode::Source;
  drawingPath_ = {};
  if (tool_ == PolygonCanvasTool::Line || tool_ == PolygonCanvasTool::Arc || tool_ == PolygonCanvasTool::CubicBezier)
    if (const EditablePath * path = activeOpenPath())
      drawingPath_ = path->id;
  setCursor(tool_ == PolygonCanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
  update();
}

/// Проверяет положительный шаг и публикует новые параметры без изменения документа.
void PolygonCanvasWidget::setSnapSettings(const PolygonCanvasSnapSettings & settings)
{
  snapSettings_ = settings;
  if (!std::isfinite(snapSettings_.gridStepMm) || snapSettings_.gridStepMm <= 0.0)
    snapSettings_.gridStepMm = 10.0;
  update();
}

/// Сохраняет направление только для следующего создаваемого сегмента.
void PolygonCanvasWidget::setArcClockwise(bool clockwise) noexcept
{
  arcClockwise_ = clockwise;
  update();
}

/// Возвращает незавершённый жест к исходной редакции и удаляет локальный предварительный просмотр.
void PolygonCanvasWidget::cancelEditorInteraction()
{
  if (dragging_)
    finishDrag(true);
  clearDrawingPreview(true);
  selectingRectangle_ = false;
  panning_ = false;
  update();
}

/// Возвращает камеру к детерминированному автоматическому вписыванию листа.
void PolygonCanvasWidget::fitToView()
{
  zoom_ = 1.0;
  pan_ = {};
  update();
}

/// Перехватывает потерю захвата мыши до обычной обработки события Qt.
bool PolygonCanvasWidget::event(QEvent * event)
{
  if (event->type() == QEvent::UngrabMouse && dragging_)
    finishDrag(true);
  return QWidget::event(event);
}

/// Выбирает один из двух режимов и поверх исходной геометрии рисует временное взаимодействие.
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
    painter.drawText(rect(), Qt::AlignCenter, tr("Откройте polygon_problem v1 или создайте задачу"));
    return;
  }
  applySceneTransform(painter, scene);
  drawSheet(painter, scene);
  if (mode_ == PolygonCanvasMode::Source)
    drawGrid(painter, scene);
  drawMargin(painter, scene);
  if (mode_ == PolygonCanvasMode::Source)
    drawEditableDocument(painter);
  else
  {
    drawRemnant(painter, scene);
    drawPlacements(painter, scene);
  }
  drawInteractionOverlay(painter);
}

/// Вычисляет масштаб вписывания, применяет масштаб пользователя и инвертирует экранную ось Y.
void PolygonCanvasWidget::applySceneTransform(QPainter & painter, const PolygonSceneView & scene) const
{
  const double scale = sceneScale();
  painter.translate(width() / 2.0 + pan_.x(), height() / 2.0 + pan_.y());
  painter.scale(scale, -scale);
  painter.translate(-scene.sheetWidth / 2.0, -scene.sheetHeight / 2.0);
}

/// Заполняет лист белым материалом и обводит его пером постоянной экранной толщины.
void PolygonCanvasWidget::drawSheet(QPainter & painter, const PolygonSceneView & scene) const
{
  QPen sheetPen(QColor("#334155"));
  sheetPen.setCosmetic(true);
  sheetPen.setWidthF(2.0);
  painter.setPen(sheetPen);
  painter.setBrush(Qt::white);
  painter.drawRect(QRectF(0.0, 0.0, scene.sheetWidth, scene.sheetHeight));
}

/// Укрупняет слишком плотный шаг до читаемого интервала не менее восьми экранных пикселей.
void PolygonCanvasWidget::drawGrid(QPainter & painter, const PolygonSceneView & scene) const
{
  if (!snapSettings_.gridVisible || snapSettings_.gridStepMm <= 0.0)
    return;
  double step = snapSettings_.gridStepMm;
  while (step * sceneScale() < 8.0)
    step *= 10.0;
  QPen gridPen(QColor(148, 163, 184, 80));
  gridPen.setCosmetic(true);
  painter.setPen(gridPen);
  const std::size_t verticalCount = static_cast<std::size_t>(std::floor(scene.sheetWidth / step));
  for (std::size_t index = 0; index <= verticalCount; ++index)
  {
    const double x = static_cast<double>(index) * step;
    painter.drawLine(QPointF(x, 0.0), QPointF(x, scene.sheetHeight));
  }
  const std::size_t horizontalCount = static_cast<std::size_t>(std::floor(scene.sheetHeight / step));
  for (std::size_t index = 0; index <= horizontalCount; ++index)
  {
    const double y = static_cast<double>(index) * step;
    painter.drawLine(QPointF(0.0, y), QPointF(scene.sheetWidth, y));
  }
}

/// Вычисляет начало правой полосы и показывает её только для завершённого результата.
void PolygonCanvasWidget::drawRemnant(QPainter & painter, const PolygonSceneView & scene) const
{
  if (snapshot_.state != PolygonWorkspaceState::Completed && snapshot_.state != PolygonWorkspaceState::Cancelled)
    return;
  const double remnantX = scene.sheetMargin + scene.usedLength;
  const double remnantRight = scene.sheetWidth - scene.sheetMargin;
  if (remnantRight <= remnantX)
    return;
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(72, 187, 120, 44));
  painter.drawRect(QRectF(remnantX, scene.sheetMargin, remnantRight - remnantX, scene.sheetHeight - 2.0 * scene.sheetMargin));
}

/// Строит внутренний прямоугольник допустимой области, если задан положительный отступ.
void PolygonCanvasWidget::drawMargin(QPainter & painter, const PolygonSceneView & scene) const
{
  if (scene.sheetMargin <= 0.0)
    return;
  QPen marginPen(QColor("#94A3B8"));
  marginPen.setCosmetic(true);
  marginPen.setStyle(Qt::DashLine);
  painter.setPen(marginPen);
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(QRectF(scene.sheetMargin, scene.sheetMargin, scene.sheetWidth - 2.0 * scene.sheetMargin,
                          scene.sheetHeight - 2.0 * scene.sheetMargin));
}

/// Формирует для каждой детали путь с правилом нечётности, сохраняя прозрачность отверстий.
void PolygonCanvasWidget::drawPlacements(QPainter & painter, const PolygonSceneView & scene) const
{
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

/// Выделяет активную деталь, показывает направления, центры дуг и управляющие линии Bézier.
void PolygonCanvasWidget::drawEditableDocument(QPainter & painter) const
{
  if (!snapshot_.editableDocument)
    return;
  std::optional<EditablePolygonDocument> preview;
  const QPointF residual = dragPreviewDelta_ - dragLastAppliedDelta_;
  if (dragging_ && !dragPointIds_.empty() && (std::abs(residual.x()) + std::abs(residual.y()) > 1.0e-12))
  {
    preview = *snapshot_.editableDocument;
    translatePreview(*preview, dragPointIds_, residual);
  }
  const EditablePolygonDocument & document = preview ? *preview : *snapshot_.editableDocument;
  const double scale = std::max(1.0, std::abs(painter.worldTransform().m11()));
  for (const EditablePart & part : document.parts)
  {
    const bool inactive = activePart_ && part.id != activePart_;
    auto drawPath = [&](const EditablePath & source)
    {
      if (source.vertices.empty())
        return;
      bool selected = contains(selectedEditorEntities_, part.id) || contains(selectedEditorEntities_, source.id);
      bool invalid = contains(diagnosticEntities_, part.id) || contains(diagnosticEntities_, source.id);
      for (const EditablePoint & point : source.vertices)
      {
        selected = selected || contains(selectedEditorEntities_, point.id);
        invalid = invalid || contains(diagnosticEntities_, point.id);
      }
      for (const EditableSegment & segment : source.segments)
      {
        selected = selected || contains(selectedEditorEntities_, segment.id);
        invalid = invalid || contains(diagnosticEntities_, segment.id);
      }
      const QPainterPath path = editablePainterPath(source);
      QPen outline(invalid ? QColor("#DC2626") : selected ? QColor("#D97706") : QColor("#2563EB"));
      outline.setCosmetic(true);
      outline.setWidthF(invalid || selected ? 3.0 : 2.0);
      if (inactive)
        outline.setColor(QColor(148, 163, 184, 130));
      painter.setPen(outline);
      painter.setBrush(source.closed && !inactive ? QColor(59, 130, 246, 28) : Qt::NoBrush);
      painter.drawPath(path);

      for (std::size_t index = 0; index < source.segments.size() && index < source.vertices.size(); ++index)
      {
        const EditableSegment & segment = source.segments[index];
        const EditablePoint & start = source.vertices[index];
        const EditablePoint & end = source.vertices[index + 1 < source.vertices.size() ? index + 1 : 0];
        QPainterPath one(QPointF(start.x, start.y));
        EditablePath temporary;
        temporary.vertices = {start, end};
        temporary.segments = {segment};
        one = editablePainterPath(temporary);
        if (!inactive)
          drawDirectionMarker(painter, one);
        if (segment.kind == EditableSegmentKind::Arc)
        {
          QPen guide(QColor("#94A3B8"));
          guide.setCosmetic(true);
          guide.setStyle(Qt::DashLine);
          painter.setPen(guide);
          painter.drawLine(QPointF(start.x, start.y), QPointF(segment.center.x, segment.center.y));
          painter.drawLine(QPointF(segment.center.x, segment.center.y), QPointF(end.x, end.y));
        }
        else if (segment.kind == EditableSegmentKind::CubicBezier)
        {
          QPen guide(QColor("#94A3B8"));
          guide.setCosmetic(true);
          guide.setStyle(Qt::DashLine);
          painter.setPen(guide);
          painter.drawPolyline(QPolygonF{QPointF(start.x, start.y), QPointF(segment.control1.x, segment.control1.y),
                                         QPointF(segment.control2.x, segment.control2.y), QPointF(end.x, end.y)});
        }
        const auto drawControl = [&](const EditablePoint & point, bool center)
        {
          const bool pointSelected = contains(selectedEditorEntities_, point.id);
          const bool pointInvalid = contains(diagnosticEntities_, point.id);
          painter.setPen(QPen(pointInvalid ? QColor("#DC2626") : QColor("#475569"), 0.0));
          painter.setBrush(pointSelected ? QColor("#F59E0B") : QColor("#E2E8F0"));
          const double radius = (pointSelected ? 5.0 : 4.0) / scale;
          if (center)
            painter.drawRect(QRectF(point.x - radius, point.y - radius, radius * 2.0, radius * 2.0));
          else
            painter.drawEllipse(QPointF(point.x, point.y), radius, radius);
        };
        if (segment.kind == EditableSegmentKind::Arc)
          drawControl(segment.center, true);
        else if (segment.kind == EditableSegmentKind::CubicBezier)
        {
          drawControl(segment.control1, false);
          drawControl(segment.control2, false);
        }
      }

      for (std::size_t index = 0; index < source.vertices.size(); ++index)
      {
        const EditablePoint & vertex = source.vertices[index];
        const bool pointSelected = contains(selectedEditorEntities_, vertex.id);
        const bool pointInvalid = contains(diagnosticEntities_, vertex.id);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pointInvalid ? QColor("#DC2626") : pointSelected ? QColor("#D97706") : QColor("#1E3A8A"));
        const double radius = (pointSelected ? 5.0 : index == 0 ? 4.0 : 3.0) / scale;
        if (index == 0)
          painter.drawRect(QRectF(vertex.x - radius, vertex.y - radius, radius * 2.0, radius * 2.0));
        else
          painter.drawEllipse(QPointF(vertex.x, vertex.y), radius, radius);
      }
    };
    if (part.outer)
      drawPath(*part.outer);
    for (const EditablePath & hole : part.holes)
      drawPath(hole);
  }
}

/// Рисует временные сущности поверх преобразованной сцены, не публикуя их в документ.
void PolygonCanvasWidget::drawInteractionOverlay(QPainter & painter) const
{
  if (mode_ != PolygonCanvasMode::Source)
    return;
  const double scale = std::max(1.0, sceneScale());
  if (currentSnap_.kind != PolygonSnapKind::None)
  {
    QPen snapPen(QColor("#16A34A"));
    snapPen.setCosmetic(true);
    snapPen.setWidthF(2.0);
    painter.setPen(snapPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(currentSnap_.point, 7.0 / scale, 7.0 / scale);
    painter.save();
    painter.resetTransform();
    painter.setPen(QColor("#166534"));
    painter.drawText(mapFromSheet(currentSnap_.point) + QPointF(12.0, -8.0), currentSnap_.label);
    painter.restore();
  }
  if (const EditablePath * path = activeOpenPath(); path && !path->vertices.empty() && tool_ != PolygonCanvasTool::Select &&
                                                    tool_ != PolygonCanvasTool::OuterPath && tool_ != PolygonCanvasTool::Hole)
  {
    const QPointF start(path->vertices.back().x, path->vertices.back().y);
    QPen preview(QColor("#059669"));
    preview.setCosmetic(true);
    preview.setWidthF(2.0);
    preview.setStyle(Qt::DashLine);
    painter.setPen(preview);
    painter.setBrush(Qt::NoBrush);
    if (tool_ == PolygonCanvasTool::Line)
      painter.drawLine(start, drawingHover_);
    else if (tool_ == PolygonCanvasTool::Arc)
    {
      if (drawingStagePoints_.empty())
        painter.drawLine(start, drawingHover_);
      else
      {
        const QPointF center = drawingStagePoints_.front();
        const double radius = std::hypot(start.x() - center.x(), start.y() - center.y());
        if (radius > 0.0)
        {
          const double angle = std::atan2(drawingHover_.y() - center.y(), drawingHover_.x() - center.x());
          const QPointF end(center.x() + std::cos(angle) * radius, center.y() + std::sin(angle) * radius);
          EditablePath temporary;
          temporary.vertices = {{{}, start.x(), start.y()}, {{}, end.x(), end.y()}};
          EditableSegment segment;
          segment.kind = EditableSegmentKind::Arc;
          segment.center = {{}, center.x(), center.y()};
          segment.clockwise = arcClockwise_;
          temporary.segments = {segment};
          painter.drawPath(editablePainterPath(temporary));
        }
      }
    }
    else if (tool_ == PolygonCanvasTool::CubicBezier)
    {
      QPolygonF controls{start};
      for (const QPointF & point : drawingStagePoints_)
        controls << point;
      controls << drawingHover_;
      painter.drawPolyline(controls);
      if (drawingStagePoints_.size() == 2)
      {
        QPainterPath curve(start);
        curve.cubicTo(drawingStagePoints_[0], drawingStagePoints_[1], drawingHover_);
        painter.drawPath(curve);
      }
    }
  }
  if (selectingRectangle_)
  {
    painter.save();
    painter.resetTransform();
    const bool crossing = selectionCurrentScreen_.x() < selectionStartScreen_.x();
    QColor fill = crossing ? QColor(22, 163, 74, 35) : QColor(37, 99, 235, 35);
    QPen pen(crossing ? QColor("#16A34A") : QColor("#2563EB"));
    pen.setStyle(crossing ? Qt::DashLine : Qt::SolidLine);
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRect(QRectF(selectionStartScreen_, selectionCurrentScreen_).normalized());
    painter.restore();
  }
}

/// Разделяет навигацию и редактирование согласно схеме настольных систем проектирования.
void PolygonCanvasWidget::mousePressEvent(QMouseEvent * event)
{
  setFocus(Qt::MouseFocusReason);
  if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && spacePressed_))
  {
    panning_ = true;
    lastMousePosition_ = event->position();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton)
  {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPointF sheet = mapToSheet(event->position());
  if (mode_ == PolygonCanvasMode::Solution)
  {
    if (const PolygonPlacedPartView * part = hitTestPlacement(sheet))
    {
      selectedPartId_ = part->partId;
      selectedInstanceIndex_ = part->instanceIndex;
      emit partSelected(QString::fromStdString(part->partId), part->instanceIndex);
      update();
    }
    event->accept();
    return;
  }
  if (!snapshot_.editableDocument)
    return;
  if (tool_ != PolygonCanvasTool::Select)
  {
    if (snapshot_.canEdit)
      handleDrawingClick(sheet, event->modifiers());
    event->accept();
    return;
  }

  const auto hit =
    findEditableEntity(*snapshot_.editableDocument, sheet, HIT_TOLERANCE_PX / sceneScale(), selectedEditorEntities_, activePart_);
  if (!hit)
  {
    if (!(event->modifiers() & Qt::ControlModifier))
      publishSelection({});
    selectingRectangle_ = true;
    selectionStartScreen_ = event->position();
    selectionCurrentScreen_ = event->position();
    event->accept();
    return;
  }
  std::vector<EntityId> selection = selectedEditorEntities_;
  if (event->modifiers() & Qt::ControlModifier)
  {
    const auto found = std::find(selection.begin(), selection.end(), hit->entity);
    if (found == selection.end())
      selection.push_back(hit->entity);
    else
      selection.erase(found);
  }
  else if (!contains(selection, hit->entity))
    selection = {hit->entity};
  publishSelection(selection);
  if (snapshot_.canEdit && contains(selectedEditorEntities_, hit->entity))
    beginDrag(*hit, sheet);
  event->accept();
}

/// Обновляет временное состояние на каждом событии, а документ — с ограниченной частотой.
void PolygonCanvasWidget::mouseMoveEvent(QMouseEvent * event)
{
  const QPointF sheet = mapToSheet(event->position());
  const bool inside =
    sheet.x() >= 0.0 && sheet.y() >= 0.0 && sheet.x() <= snapshot_.scene.sheetWidth && sheet.y() <= snapshot_.scene.sheetHeight;
  emit cursorPositionChanged(sheet.x(), sheet.y(), inside);
  if (panning_)
  {
    pan_ += event->position() - lastMousePosition_;
    lastMousePosition_ = event->position();
    update();
    event->accept();
    return;
  }
  if (dragging_)
  {
    updateDrag(sheet, event->modifiers(), false);
    event->accept();
    return;
  }
  if (selectingRectangle_)
  {
    selectionCurrentScreen_ = event->position();
    update();
    event->accept();
    return;
  }
  if (mode_ == PolygonCanvasMode::Source && tool_ != PolygonCanvasTool::Select && snapshot_.editableDocument)
  {
    EntityId closing;
    if (const EditablePath * path = activeOpenPath(); path && path->vertices.size() >= 3)
      closing = path->vertices.front().id;
    currentSnap_ = snapped(sheet, event->modifiers(), closing);
    drawingHover_ = currentSnap_.point;
    update();
  }
  QWidget::mouseMoveEvent(event);
}

/// Завершает текущую операцию и гарантирует итоговую публикацию координаты перетаскивания.
void PolygonCanvasWidget::mouseReleaseEvent(QMouseEvent * event)
{
  if ((event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) && panning_)
  {
    panning_ = false;
    setCursor(tool_ == PolygonCanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && dragging_)
  {
    updateDrag(mapToSheet(event->position()), event->modifiers(), true);
    finishDrag(false);
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton && selectingRectangle_)
  {
    selectingRectangle_ = false;
    const QPointF first = mapToSheet(selectionStartScreen_);
    const QPointF second = mapToSheet(selectionCurrentScreen_);
    const bool crossing = selectionCurrentScreen_.x() < selectionStartScreen_.x();
    std::vector<EntityId> found = findEditableEntities(*snapshot_.editableDocument, QRectF(first, second), crossing, activePart_);
    if (event->modifiers() & Qt::ControlModifier)
    {
      std::vector<EntityId> merged = selectedEditorEntities_;
      for (EntityId id : found)
      {
        const auto existing = std::find(merged.begin(), merged.end(), id);
        if (existing == merged.end())
          merged.push_back(id);
        else
          merged.erase(existing);
      }
      found = std::move(merged);
    }
    publishSelection(found);
    update();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

/// Компенсирует изменение масштаба смещением камеры, сохраняя точку под указателем.
void PolygonCanvasWidget::wheelEvent(QWheelEvent * event)
{
  const QPointF before = mapToSheet(event->position());
  const double factor = std::pow(1.0015, event->angleDelta().y());
  zoom_ = std::clamp(zoom_ * factor, 0.2, 20.0);
  const QPointF afterScreen = mapFromSheet(before);
  pan_ += event->position() - afterScreen;
  update();
  event->accept();
}

/// Отдаёт `Esc` редактору только вне выполняющегося раскроя и включает временную навигацию.
void PolygonCanvasWidget::keyPressEvent(QKeyEvent * event)
{
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
  {
    spacePressed_ = true;
    setCursor(Qt::OpenHandCursor);
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape && snapshot_.state != PolygonWorkspaceState::Running)
  {
    cancelEditorInteraction();
    tool_ = PolygonCanvasTool::Select;
    emit editorToolChangeRequested(tool_);
    setCursor(Qt::ArrowCursor);
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

/// Возвращает курсор активного инструмента после временной навигации.
void PolygonCanvasWidget::keyReleaseEvent(QKeyEvent * event)
{
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
  {
    spacePressed_ = false;
    if (!panning_)
      setCursor(tool_ == PolygonCanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

/// Не оставляет в прикладной истории незавершённую транзакцию после ухода фокуса.
void PolygonCanvasWidget::focusOutEvent(QFocusEvent * event)
{
  spacePressed_ = false;
  if (dragging_)
    finishDrag(true);
  QWidget::focusOutEvent(event);
}

/// Обращает преобразование камеры без изменения геометрии модели представления.
QPointF PolygonCanvasWidget::mapToSheet(const QPointF & position) const
{
  const PolygonSceneView & scene = snapshot_.scene;
  const double scale = sceneScale();
  if (scene.sheetWidth <= 0.0 || scene.sheetHeight <= 0.0 || scale <= 0.0)
    return {};
  return {(position.x() - width() / 2.0 - pan_.x()) / scale + scene.sheetWidth / 2.0,
          -(position.y() - height() / 2.0 - pan_.y()) / scale + scene.sheetHeight / 2.0};
}

/// Применяет прямое преобразование камеры для закрепления масштаба относительно указателя.
QPointF PolygonCanvasWidget::mapFromSheet(const QPointF & point) const
{
  const PolygonSceneView & scene = snapshot_.scene;
  const double scale = sceneScale();
  return {width() / 2.0 + pan_.x() + (point.x() - scene.sheetWidth / 2.0) * scale,
          height() / 2.0 + pan_.y() - (point.y() - scene.sheetHeight / 2.0) * scale};
}

/// Учитывает доступную область, размеры листа и пользовательский коэффициент.
double PolygonCanvasWidget::sceneScale() const
{
  const PolygonSceneView & scene = snapshot_.scene;
  if (scene.sheetWidth <= 0.0 || scene.sheetHeight <= 0.0)
    return 1.0;
  const double availableWidth = std::max(1.0, width() - 2.0 * VIEW_PADDING);
  const double availableHeight = std::max(1.0, height() - 2.0 * VIEW_PADDING);
  return std::min(availableWidth / scene.sheetWidth, availableHeight / scene.sheetHeight) * zoom_;
}

/// Проверяет детали в обратном порядке отрисовки и учитывает прозрачные отверстия.
const PolygonPlacedPartView * PolygonCanvasWidget::hitTestPlacement(const QPointF & sheetPoint) const
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

/// Использует закреплённый путь рисования либо путь первой выбранной сущности.
const EditablePath * PolygonCanvasWidget::activeOpenPath() const
{
  if (!snapshot_.editableDocument)
    return nullptr;
  if (drawingPath_)
  {
    const EditablePath * path = findPath(*snapshot_.editableDocument, drawingPath_);
    return path && !path->closed ? path : nullptr;
  }
  for (EntityId selected : selectedEditorEntities_)
  {
    const auto reference = findEditorEntity(*snapshot_.editableDocument, selected);
    if (!reference || !reference->path)
      continue;
    const EditablePath * path = findPath(*snapshot_.editableDocument, reference->path);
    if (path && !path->closed)
      return path;
  }
  return nullptr;
}

/// Временно отключает обе привязки клавишей `Alt` и исключает перемещаемые точки.
PolygonCanvasSnapResult PolygonCanvasWidget::snapped(const QPointF & raw, Qt::KeyboardModifiers modifiers,
                                                     EntityId closingVertex) const
{
  if (!snapshot_.editableDocument || (modifiers & Qt::AltModifier))
    return {raw};
  return snapEditablePoint(*snapshot_.editableDocument, raw, SNAP_TOLERANCE_PX / sceneScale(), snapSettings_, activePart_,
                           dragPointIds_, closingVertex);
}

/// Дедуплицирует выбор, выводит его активную деталь и синхронизирует остальные панели.
void PolygonCanvasWidget::publishSelection(const std::vector<EntityId> & selection)
{
  std::vector<EntityId> unique;
  for (EntityId id : selection)
    if (id && !contains(unique, id))
      unique.push_back(id);
  selectedEditorEntities_ = std::move(unique);
  if (snapshot_.editableDocument && !selectedEditorEntities_.empty())
  {
    const auto reference = findEditorEntity(*snapshot_.editableDocument, selectedEditorEntities_.front());
    if (reference)
      activePart_ = reference->part;
  }
  std::vector<std::uint64_t> values;
  values.reserve(selectedEditorEntities_.size());
  for (EntityId id : selectedEditorEntities_)
    values.push_back(id.value);
  emit editorEntitiesSelected(values);
  update();
}

/// Раскрывает выбор в точки и открывает объединяемую транзакцию только при наличии действия приложения.
void PolygonCanvasWidget::beginDrag(const PolygonCanvasEntityHit & hit, const QPointF & sheetPoint)
{
  if (!snapshot_.editableDocument || !editorActions_.beginEditGesture || !editorActions_.editDocument)
    return;
  dragPointIds_ = collectMovablePointIds(*snapshot_.editableDocument, selectedEditorEntities_);
  if (dragPointIds_.empty())
    return;
  dragGesture_ = editorActions_.beginEditGesture();
  if (dragGesture_ == 0)
    return;
  dragging_ = true;
  dragStartSheet_ = sheetPoint;
  dragLastAppliedDelta_ = {};
  dragPreviewDelta_ = {};
  dragAnchor_ = sheetPoint;
  if (hit.kind == EditorEntityKind::Point)
    if (const auto point = editablePointPosition(*snapshot_.editableDocument, hit.entity))
      dragAnchor_ = *point;
  dragPublishTimer_.restart();
  grabMouse();
}

/// Привязывает опорную точку и отправляет абсолютную одиночную либо относительную групповую команду.
void PolygonCanvasWidget::updateDrag(const QPointF & sheetPoint, Qt::KeyboardModifiers modifiers, bool finalUpdate)
{
  if (!dragging_ || !snapshot_.editableDocument || !editorActions_.editDocument)
    return;
  const QPointF rawAnchor = dragAnchor_ + (sheetPoint - dragStartSheet_);
  currentSnap_ = snapped(rawAnchor, modifiers);
  const QPointF total = currentSnap_.point - dragAnchor_;
  dragPreviewDelta_ = total;
  update();
  if (!finalUpdate && dragPublishTimer_.isValid() && dragPublishTimer_.elapsed() < DRAG_PUBLISH_INTERVAL_MS)
    return;
  const QPointF incremental = total - dragLastAppliedDelta_;
  if (std::abs(incremental.x()) + std::abs(incremental.y()) <= 1.0e-12)
    return;
  EditorCommand command;
  if (dragPointIds_.size() == 1)
  {
    const auto original = editablePointPosition(*snapshot_.editableDocument, dragPointIds_.front());
    if (!original)
      return;
    command = MovePointCommand{dragPointIds_.front(), {original->x() + incremental.x(), original->y() + incremental.y()}};
  }
  else
    command = TranslatePointsCommand{dragPointIds_, incremental.x(), incremental.y()};
  editorActions_.editDocument({"Перемещение геометрии", {std::move(command)}, dragGesture_});
  dragLastAppliedDelta_ = total;
  dragPublishTimer_.restart();
}

/// Завершает объединение либо просит сессию восстановить снимок до начала жеста.
void PolygonCanvasWidget::finishDrag(bool cancel)
{
  if (!dragging_)
    return;
  dragging_ = false;
  releaseMouse();
  if (dragGesture_ != 0)
  {
    if (cancel && editorActions_.cancelEditGesture &&
        (std::abs(dragLastAppliedDelta_.x()) + std::abs(dragLastAppliedDelta_.y()) > 1.0e-12))
      editorActions_.cancelEditGesture(dragGesture_);
    else if (editorActions_.finishEditGesture)
      editorActions_.finishEditGesture(dragGesture_);
  }
  dragGesture_ = 0;
  dragPointIds_.clear();
  dragPreviewDelta_ = {};
  dragLastAppliedDelta_ = {};
  currentSnap_ = {};
  update();
}

/// Интерпретирует щелчки в зависимости от выбранного вида сегмента и текущей стадии.
void PolygonCanvasWidget::handleDrawingClick(const QPointF & sheetPoint, Qt::KeyboardModifiers modifiers)
{
  if (!snapshot_.editableDocument)
    return;
  if (tool_ == PolygonCanvasTool::OuterPath || tool_ == PolygonCanvasTool::Hole)
  {
    const PolygonCanvasSnapResult value = snapped(sheetPoint, modifiers);
    createPathAt(value.point, tool_ == PolygonCanvasTool::Hole);
    return;
  }
  const EditablePath * path = activeOpenPath();
  if (!path || path->vertices.empty())
  {
    emit editorInteractionMessage(tr("Выберите открытую цепочку или создайте новый контур"));
    return;
  }
  drawingPath_ = path->id;
  drawingPart_ = activePart_;
  const EntityId closing = path->vertices.size() >= 3 ? path->vertices.front().id : EntityId{};
  PolygonCanvasSnapResult value = snapped(sheetPoint, modifiers, closing);
  currentSnap_ = value;
  const bool close = closing && value.kind == PolygonSnapKind::ClosingVertex;
  if (tool_ == PolygonCanvasTool::Line)
  {
    commitDrawingSegment(value.point, close);
    return;
  }
  if (tool_ == PolygonCanvasTool::Arc)
  {
    if (drawingStagePoints_.empty())
    {
      const QPointF start(path->vertices.back().x, path->vertices.back().y);
      if (std::hypot(value.point.x() - start.x(), value.point.y() - start.y()) <= 1.0e-9)
      {
        emit editorInteractionMessage(tr("Центр дуги должен отличаться от начальной точки"));
        return;
      }
      drawingStagePoints_.push_back(value.point);
      update();
      return;
    }
    const QPointF start(path->vertices.back().x, path->vertices.back().y);
    const QPointF center = drawingStagePoints_.front();
    const double radius = std::hypot(start.x() - center.x(), start.y() - center.y());
    if (value.kind != PolygonSnapKind::None && std::abs(std::hypot(value.point.x() - center.x(), value.point.y() - center.y()) -
                                                        radius) > SNAP_TOLERANCE_PX / sceneScale())
    {
      value = {sheetPoint};
      currentSnap_ = {};
    }
    const bool arcClose = closing && value.kind == PolygonSnapKind::ClosingVertex;
    if (arcClose && std::hypot(value.point.x() - start.x(), value.point.y() - start.y()) <= 1.0e-9)
    {
      emit editorInteractionMessage(tr("Полнооборотную дугу необходимо построить двумя сегментами"));
      return;
    }
    const double direction = std::atan2(value.point.y() - center.y(), value.point.x() - center.x());
    value.point = {center.x() + std::cos(direction) * radius, center.y() + std::sin(direction) * radius};
    commitDrawingSegment(value.point, arcClose);
    return;
  }
  if (tool_ == PolygonCanvasTool::CubicBezier)
  {
    if (drawingStagePoints_.size() < 2)
    {
      drawingStagePoints_.push_back(value.point);
      update();
      return;
    }
    commitDrawingSegment(value.point, close);
  }
}

/// Создаёт цепочку прикладной командой и удерживает жест до появления первого сегмента.
void PolygonCanvasWidget::createPathAt(const QPointF & point, bool hole)
{
  if (!snapshot_.editableDocument || !activePart_ || !editorActions_.beginEditGesture || !editorActions_.editDocument)
  {
    emit editorInteractionMessage(tr("Сначала выберите тип детали"));
    return;
  }
  const EditablePart * part = findPart(*snapshot_.editableDocument, activePart_);
  if (!part)
    return;
  if (!hole && part->outer)
  {
    emit editorInteractionMessage(tr("У выбранной детали уже есть внешний контур"));
    return;
  }
  const std::uint64_t gesture = editorActions_.beginEditGesture();
  if (gesture == 0)
    return;
  provisionalPathGesture_ = gesture;
  drawingPart_ = activePart_;
  editorActions_.editDocument({hole ? "Создание отверстия" : "Создание внешнего контура",
                               {CreatePathCommand{activePart_, hole, {point.x(), point.y()}}},
                               gesture});
  refreshDrawingPath(hole);
  if (!drawingPath_)
  {
    if (editorActions_.cancelEditGesture)
      editorActions_.cancelEditGesture(gesture);
    provisionalPathGesture_.reset();
    emit editorInteractionMessage(tr("Контур создать не удалось"));
    return;
  }
  publishSelection({drawingPath_});
  tool_ = PolygonCanvasTool::Line;
  emit editorToolChangeRequested(tool_);
  update();
}

/// Формирует ровно одну команду сегмента и завершает объединение создания первой цепочки.
void PolygonCanvasWidget::commitDrawingSegment(const QPointF & end, bool closePath)
{
  const EditablePath * path = activeOpenPath();
  if (!path || !editorActions_.editDocument)
    return;
  const EditablePoint & start = path->vertices.back();
  if (!closePath && std::hypot(end.x() - start.x, end.y() - start.y) <= 1.0e-9)
  {
    emit editorInteractionMessage(tr("Конечная точка должна отличаться от начальной"));
    return;
  }
  EditorSegmentValue segment;
  if (tool_ == PolygonCanvasTool::Arc)
  {
    if (drawingStagePoints_.size() != 1)
      return;
    segment.kind = EditableSegmentKind::Arc;
    segment.center = {drawingStagePoints_[0].x(), drawingStagePoints_[0].y()};
    segment.clockwise = arcClockwise_;
  }
  else if (tool_ == PolygonCanvasTool::CubicBezier)
  {
    if (drawingStagePoints_.size() != 2)
      return;
    segment.kind = EditableSegmentKind::CubicBezier;
    segment.control1 = {drawingStagePoints_[0].x(), drawingStagePoints_[0].y()};
    segment.control2 = {drawingStagePoints_[1].x(), drawingStagePoints_[1].y()};
  }
  const std::optional<std::uint64_t> gesture = provisionalPathGesture_;
  EditorCommand command = closePath ? EditorCommand{ClosePathCommand{path->id, segment}}
                                    : EditorCommand{AppendSegmentCommand{path->id, {end.x(), end.y()}, segment}};
  editorActions_.editDocument({closePath ? "Замыкание контура" : "Добавление сегмента", {std::move(command)}, gesture});
  if (gesture && editorActions_.finishEditGesture)
    editorActions_.finishEditGesture(*gesture);
  provisionalPathGesture_.reset();
  drawingStagePoints_.clear();
  currentSnap_ = {};
  if (closePath)
    drawingPath_ = {};
  update();
}

/// Находит новый путь по выбранной детали после синхронного выполнения прикладного действия.
void PolygonCanvasWidget::refreshDrawingPath(bool hole)
{
  drawingPath_ = {};
  if (!snapshot_.editableDocument)
    return;
  const EditablePart * part = findPart(*snapshot_.editableDocument, drawingPart_);
  if (!part)
    return;
  if (!hole && part->outer)
    drawingPath_ = part->outer->id;
  else if (hole && !part->holes.empty())
    drawingPath_ = std::max_element(part->holes.begin(), part->holes.end(), [](const EditablePath & lhs, const EditablePath & rhs)
                                    { return lhs.id.value < rhs.id.value; })
                     ->id;
}

/// Удаляет только неподтверждённые стадии и при необходимости откатывает одноточечную цепочку.
void PolygonCanvasWidget::clearDrawingPreview(bool cancelProvisionalPath)
{
  drawingStagePoints_.clear();
  currentSnap_ = {};
  if (cancelProvisionalPath && provisionalPathGesture_)
  {
    if (editorActions_.cancelEditGesture)
      editorActions_.cancelEditGesture(*provisionalPathGesture_);
    provisionalPathGesture_.reset();
    drawingPath_ = {};
  }
  update();
}
