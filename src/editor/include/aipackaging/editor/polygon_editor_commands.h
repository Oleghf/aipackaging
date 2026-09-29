#ifndef AIPACKAGING_EDITOR_POLYGON_EDITOR_COMMANDS_H
#define AIPACKAGING_EDITOR_POLYGON_EDITOR_COMMANDS_H

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <aipackaging/editor/polygon_document.h>
#include <aipackaging/editor/polygon_document_validation.h>

namespace aipackaging::editor
{
/// Передаёт координаты новой или изменяемой точки без назначения устойчивого идентификатора.
struct EditorPointValue
{
  double x = 0.0;
  double y = 0.0;
};

/// Описывает геометрию сегмента; идентификаторы служебных точек назначает редактор.
struct EditorSegmentValue
{
  EditableSegmentKind kind = EditableSegmentKind::Line;
  EditorPointValue center;
  EditorPointValue control1;
  EditorPointValue control2;
  bool clockwise = false;
};

/// Заменяет пользовательский идентификатор задачи.
struct SetProblemIdCommand
{
  std::string problemId;
};

/// Заменяет размеры листа в миллиметрах.
struct SetSheetCommand
{
  double width = 0.0;
  double height = 0.0;
};

/// Заменяет производственные параметры документа одной транзакцией.
struct SetManufacturingCommand
{
  EditableManufacturing manufacturing;
};

/// Добавляет пустой тип детали с назначенным редактором идентификатором.
struct AddPartCommand
{
  std::string partId;
  std::uint32_t quantity = 1;
  std::vector<int> allowedRotations{0};
};

/// Удаляет тип детали вместе со всей принадлежащей ему геометрией.
struct DeletePartCommand
{
  EntityId part;
};

/// Копирует тип детали, назначая всем сущностям новые идентификаторы.
struct DuplicatePartCommand
{
  EntityId part;
  std::string partId;
};

/// Заменяет редактируемые свойства существующего типа детали.
struct SetPartPropertiesCommand
{
  EntityId part;
  std::string partId;
  std::uint32_t quantity = 0;
  std::vector<int> allowedRotations;
};

/// Создаёт открытую внешнюю цепочку либо отверстие с первой вершиной.
struct CreatePathCommand
{
  EntityId part;
  bool hole = false;
  EditorPointValue firstVertex;
};

/// Удаляет внешнюю цепочку либо отверстие по идентификатору.
struct DeletePathCommand
{
  EntityId path;
};

/// Добавляет сегмент и новую конечную вершину в открытую цепочку.
struct AppendSegmentCommand
{
  EntityId path;
  EditorPointValue endVertex;
  EditorSegmentValue segment;
};

/// Замыкает открытую цепочку сегментом к первой вершине.
struct ClosePathCommand
{
  EntityId path;
  EditorSegmentValue segment;
};

/// Удаляет замыкающий сегмент, сохраняя все вершины цепочки.
struct OpenPathCommand
{
  EntityId path;
};

/// Заменяет вид и параметры существующего сегмента.
struct UpdateSegmentCommand
{
  EntityId segment;
  EditorSegmentValue value;
};

/// Удаляет сегмент и его конечную вершину с детерминированным соединением соседей.
struct DeleteSegmentCommand
{
  EntityId segment;
};

/// Перемещает вершину, центр дуги либо контрольную точку в абсолютные координаты.
struct MovePointCommand
{
  EntityId point;
  EditorPointValue value;
};

/// Переносит набор точек на общее конечное смещение.
struct TranslatePointsCommand
{
  std::vector<EntityId> points;
  double deltaX = 0.0;
  double deltaY = 0.0;
};

/// Объединяет поддерживаемые изменения документа в один закрытый контракт редактора.
using EditorCommand = std::variant<SetProblemIdCommand, SetSheetCommand, SetManufacturingCommand, AddPartCommand,
                                   DeletePartCommand, DuplicatePartCommand, SetPartPropertiesCommand, CreatePathCommand,
                                   DeletePathCommand, AppendSegmentCommand, ClosePathCommand, OpenPathCommand,
                                   UpdateSegmentCommand, DeleteSegmentCommand, MovePointCommand, TranslatePointsCommand>;

/// Группирует изменения, которые должны примениться и попасть в историю атомарно.
struct EditorCommandBatch
{
  std::string label;
  std::vector<EditorCommand> commands;
  std::optional<std::uint64_t> gestureId;
};

/// Описывает доступность отмены, повтора и положение относительно чистой точки.
struct EditorHistoryState
{
  bool canUndo = false;
  bool canRedo = false;
  bool dirty = false;
  std::uint64_t revision = 0;
  std::string undoLabel;
  std::string redoLabel;
};

/// Возвращает результат одной атомарной попытки изменения документа.
struct EditorCommandResult
{
  bool accepted = false;
  std::string error;
  std::vector<DocumentDiagnostic> diagnostics;
  EditorHistoryState history;
};

/// Владеет редактируемым документом, ограниченной историей и чистой точкой сохранения.
class PolygonEditorSession
{
public:
  /// Создаёт пустую сессию с новым документом и чистой начальной редакцией.
  PolygonEditorSession();
  /// Создаёт сессию поверх загруженного документа и задаёт его исходную чистоту.
  explicit PolygonEditorSession(EditablePolygonDocument document, bool clean = true);

  /// Возвращает неизменяемый текущий документ.
  const EditablePolygonDocument & document() const noexcept;
  /// Возвращает доступность истории и текущую редакцию.
  EditorHistoryState history() const;
  /// Применяет пакет полностью либо сохраняет прежнее состояние.
  EditorCommandResult execute(const EditorCommandBatch & batch);
  /// Отменяет последнюю подтверждённую транзакцию.
  EditorCommandResult undo();
  /// Повторяет следующую отменённую транзакцию.
  EditorCommandResult redo();
  /// Выдаёт ненулевой идентификатор нового непрерывного изменения.
  std::uint64_t beginGesture() noexcept;
  /// Отменяет ещё не завершённую объединённую транзакцию указанного жеста.
  EditorCommandResult cancelGesture(std::uint64_t gestureId);
  /// Завершает объединение последующих изменений с указанным жестом.
  void finishGesture(std::uint64_t gestureId) noexcept;
  /// Делает текущую редакцию чистой после успешного пользовательского сохранения.
  void markSaved() noexcept;
  /// Заменяет документ, очищает историю и устанавливает требуемую чистоту.
  void reset(EditablePolygonDocument document, bool clean);

private:
  /// Хранит одну пользовательскую транзакцию и оба её устойчивых состояния.
  struct HistoryEntry
  {
    std::string label;
    EditablePolygonDocument before;
    EditablePolygonDocument after;
    std::uint64_t beforeRevision = 0;
    std::uint64_t afterRevision = 0;
    std::optional<std::uint64_t> gestureId;
  };

  EditablePolygonDocument document_;
  std::vector<HistoryEntry> entries_;
  std::size_t position_ = 0;
  std::uint64_t nextRevision_ = 1;
  std::uint64_t currentRevision_ = 0;
  std::optional<std::uint64_t> cleanRevision_;
  std::uint64_t identifierHighWater_ = 1;
  std::uint64_t nextGestureId_ = 1;
  std::optional<std::uint64_t> finishedGesture_;
};
} // namespace aipackaging::editor

#endif
