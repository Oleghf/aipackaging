#ifndef AIPACKAGING_EDITOR_POLYGON_DOCUMENT_H
#define AIPACKAGING_EDITOR_POLYGON_DOCUMENT_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aipackaging::editor
{
/// Устойчиво идентифицирует сущность внутри одного редактируемого документа.
struct EntityId
{
  std::uint64_t value = 0;

  /// Сообщает, назначено ли сущности ненулевое значение.
  explicit operator bool() const noexcept { return value != 0; }
  /// Сравнивает числовые значения идентификаторов.
  bool operator==(const EntityId &) const = default;
};

/// Описывает редактируемую точку в миллиметрах и сохраняет её идентичность.
struct EditablePoint
{
  EntityId id;
  double x = 0.0;
  double y = 0.0;
};

/// Обозначает поддерживаемый вид редактируемого сегмента.
enum class EditableSegmentKind : std::uint8_t
{
  Line,
  Arc,
  CubicBezier
};

/// Описывает сегмент между соседними вершинами редактируемой цепочки.
struct EditableSegment
{
  EntityId id;
  EditableSegmentKind kind = EditableSegmentKind::Line;
  EditablePoint center;
  EditablePoint control1;
  EditablePoint control2;
  bool clockwise = false;
};

/// Хранит открытую либо замкнутую упорядоченную цепочку вершин и сегментов.
struct EditablePath
{
  EntityId id;
  bool closed = false;
  std::vector<EditablePoint> vertices;
  std::vector<EditableSegment> segments;
};

/// Описывает один редактируемый тип детали и его технологические параметры.
struct EditablePart
{
  EntityId id;
  std::string partId;
  std::uint32_t quantity = 0;
  std::optional<EditablePath> outer;
  std::vector<EditablePath> holes;
  std::vector<int> allowedRotations;
};

/// Описывает прямоугольный лист редактируемого документа.
struct EditableSheet
{
  EntityId id;
  double width = 0.0;
  double height = 0.0;
  std::string unit = "mm";
};

/// Хранит производственные параметры редактируемой задачи.
struct EditableManufacturing
{
  double sheetMargin = 0.0;
  double partSpacing = 0.0;
  double kerf = 0.0;
  double curveTolerance = 0.05;
};

/// Хранит версию поддерживаемой целевой функции редактируемой задачи.
struct EditableObjective
{
  std::string type = "valuable_right_remnant";
  int version = 1;
};

/// Владеет редактируемым содержимым задачи и монотонным источником идентификаторов.
class EditablePolygonDocument
{
public:
  /// Создаёт пустой документ с уже идентифицированным листом.
  EditablePolygonDocument();

  /// Выдаёт новый идентификатор, который больше не будет повторно использован.
  EntityId allocateEntityId();
  /// Возвращает следующее значение монотонного источника идентификаторов.
  std::uint64_t nextEntityId() const noexcept;
  /// Восстанавливает следующее значение только при строгом увеличении текущего.
  bool restoreNextEntityId(std::uint64_t value) noexcept;

  std::string problemId;
  EditableSheet sheet;
  EditableManufacturing manufacturing;
  std::vector<EditablePart> parts;
  EditableObjective objective;

private:
  std::uint64_t nextEntityId_ = 1;
};
} // namespace aipackaging::editor

#endif
