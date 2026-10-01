#ifndef AIPACKAGING_GUI_POLYGONEDITORCANVASINTERACTION_H
#define AIPACKAGING_GUI_POLYGONEDITORCANVASINTERACTION_H

#include <cstdint>
#include <memory>
#include <optional>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <vector>

#include <aipackaging/editor/polygon_editor_interaction.h>

/// Обозначает активный инструмент редактирования исходной геометрии на полотне.
enum class PolygonCanvasTool : std::uint8_t
{
  Select,
  OuterPath,
  Hole,
  Line,
  Arc,
  CubicBezier
};

/// Обозначает показываемый полотном источник геометрии.
enum class PolygonCanvasMode : std::uint8_t
{
  Source,
  Solution
};

/// Обозначает источник применённой координатной привязки.
enum class PolygonSnapKind : std::uint8_t
{
  None,
  ClosingVertex,
  Endpoint,
  ArcCenter,
  Grid
};

/// Хранит пользовательские параметры сетки и координатных привязок.
struct PolygonCanvasSnapSettings
{
  bool gridVisible = true;
  double gridStepMm = 10.0;
  bool geometryEnabled = true;
  bool gridEnabled = false;
};

/// Описывает устойчивую сущность, найденную под указателем.
struct PolygonCanvasEntityHit
{
  aipackaging::editor::EntityId entity;
  aipackaging::editor::EditorEntityKind kind = aipackaging::editor::EditorEntityKind::Unknown;
  aipackaging::editor::EntityId part;
  aipackaging::editor::EntityId path;
  double distance = 0.0;
};

/// Возвращает подтверждаемую координату и объяснение выбранной привязки.
struct PolygonCanvasSnapResult
{
  QPointF point;
  PolygonSnapKind kind = PolygonSnapKind::None;
  aipackaging::editor::EntityId entity;
  QString label;
};

/// Индексирует геометрию одной неизменяемой редакции для выбора, рамки и привязок.
class PolygonCanvasSpatialIndex
{
public:
  /// Строит пустой индекс, пригодный до первой публикации документа.
  PolygonCanvasSpatialIndex() = default;
  /// Перестраивает индекс для новой неизменяемой редакции документа.
  void rebuild(std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document);
  /// Возвращает индексируемый снимок документа либо пустое значение.
  const std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> & document() const noexcept;
  /// Находит сущность по допуску с сохранением установленного приоритета выбора.
  std::optional<PolygonCanvasEntityHit> find(const QPointF & point, double toleranceMm,
                                             const std::vector<aipackaging::editor::EntityId> & selected,
                                             aipackaging::editor::EntityId activePart) const;
  /// Возвращает сущности внутри рамки либо пересекающие её справа налево.
  std::vector<aipackaging::editor::EntityId> findInRectangle(const QRectF & rectangle, bool crossing,
                                                             aipackaging::editor::EntityId activePart) const;
  /// Выбирает ближайшую индексированную цель геометрической привязки.
  PolygonCanvasSnapResult snap(const QPointF & point, double toleranceMm, const PolygonCanvasSnapSettings & settings,
                               aipackaging::editor::EntityId activePart,
                               const std::vector<aipackaging::editor::EntityId> & excluded,
                               aipackaging::editor::EntityId closingVertex = {}) const;

private:
  /// Хранит одну индексированную точку и её принадлежность документу.
  struct PointEntry
  {
    QPointF point;
    aipackaging::editor::EntityId entity;
    aipackaging::editor::EntityId part;
    aipackaging::editor::EntityId path;
    PolygonSnapKind snapKind = PolygonSnapKind::None;
    std::size_t order = 0;
  };
  /// Хранит вычисленный один раз путь сегмента и его габарит.
  struct SegmentEntry
  {
    aipackaging::editor::EntityId entity;
    aipackaging::editor::EntityId part;
    aipackaging::editor::EntityId path;
    QPainterPath geometry;
    QRectF bounds;
    std::size_t order = 0;
  };
  /// Хранит вычисленную геометрию замкнутого контура и её габарит.
  struct PathEntry
  {
    aipackaging::editor::EntityId entity;
    aipackaging::editor::EntityId part;
    QPainterPath geometry;
    QRectF bounds;
    bool closed = false;
    std::size_t order = 0;
  };

  std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document_;
  std::vector<PointEntry> points_;
  std::vector<SegmentEntry> segments_;
  std::vector<PathEntry> paths_;
};

/// Строит путь Qt из одной редактируемой цепочки без изменения документа.
QPainterPath editablePainterPath(const aipackaging::editor::EditablePath & path);
/// Находит сущность по экранно-независимому допуску в миллиметрах.
std::optional<PolygonCanvasEntityHit> findEditableEntity(const aipackaging::editor::EditablePolygonDocument & document,
                                                         const QPointF & point, double toleranceMm,
                                                         const std::vector<aipackaging::editor::EntityId> & selected,
                                                         aipackaging::editor::EntityId activePart);
/// Возвращает сущности внутри рамки либо пересекающие её в зависимости от направления выбора.
std::vector<aipackaging::editor::EntityId> findEditableEntities(const aipackaging::editor::EditablePolygonDocument & document,
                                                                const QRectF & rectangle, bool crossing,
                                                                aipackaging::editor::EntityId activePart);
/// Применяет устойчивый приоритет геометрических и сеточных привязок.
PolygonCanvasSnapResult snapEditablePoint(const aipackaging::editor::EditablePolygonDocument & document, const QPointF & point,
                                          double toleranceMm, const PolygonCanvasSnapSettings & settings,
                                          aipackaging::editor::EntityId activePart,
                                          const std::vector<aipackaging::editor::EntityId> & excluded,
                                          aipackaging::editor::EntityId closingVertex = {});
/// Возвращает координаты устойчивой редактируемой точки либо отсутствие.
std::optional<QPointF> editablePointPosition(const aipackaging::editor::EditablePolygonDocument & document,
                                             aipackaging::editor::EntityId point);

#endif
