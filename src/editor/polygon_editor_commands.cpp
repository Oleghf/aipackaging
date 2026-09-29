#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include <aipackaging/editor/polygon_editor_commands.h>

namespace aipackaging::editor
{
namespace
{
constexpr std::size_t MAX_HISTORY_ENTRIES = 200;

/// Сообщает, пригодно ли число для хранения координаты или технологического параметра.
bool finite(double value) noexcept
{
  return std::isfinite(value);
}

/// Находит тип детали по устойчивому идентификатору.
EditablePart * findPart(EditablePolygonDocument & document, EntityId id)
{
  const auto found =
    std::find_if(document.parts.begin(), document.parts.end(), [id](const EditablePart & part) { return part.id == id; });
  return found == document.parts.end() ? nullptr : &*found;
}

/// Перебирает все цепочки документа до совпадения идентификатора.
EditablePath * findPath(EditablePolygonDocument & document, EntityId id)
{
  for (EditablePart & part : document.parts)
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

/// Перебирает точки указанного сегмента, учитывая только данные его вида.
template<typename Callback>
void visitSegmentPoints(EditableSegment & segment, Callback callback)
{
  if (segment.kind == EditableSegmentKind::Arc)
    callback(segment.center);
  else if (segment.kind == EditableSegmentKind::CubicBezier)
  {
    callback(segment.control1);
    callback(segment.control2);
  }
}

/// Находит любую редактируемую точку документа по устойчивому идентификатору.
EditablePoint * findPoint(EditablePolygonDocument & document, EntityId id)
{
  for (EditablePart & part : document.parts)
  {
    auto inspect = [&id](EditablePath & path) -> EditablePoint *
    {
      const auto vertex =
        std::find_if(path.vertices.begin(), path.vertices.end(), [id](const EditablePoint & point) { return point.id == id; });
      if (vertex != path.vertices.end())
        return &*vertex;
      for (EditableSegment & segment : path.segments)
      {
        EditablePoint * result = nullptr;
        visitSegmentPoints(segment,
                           [&result, id](EditablePoint & point)
                           {
                             if (point.id == id)
                               result = &point;
                           });
        if (result)
          return result;
      }
      return nullptr;
    };
    if (part.outer)
      if (EditablePoint * point = inspect(*part.outer))
        return point;
    for (EditablePath & hole : part.holes)
      if (EditablePoint * point = inspect(hole))
        return point;
  }
  return nullptr;
}

/// Находит сегмент и содержащую его цепочку.
std::pair<EditablePath *, std::size_t> findSegment(EditablePolygonDocument & document, EntityId id)
{
  for (EditablePart & part : document.parts)
  {
    auto inspect = [id](EditablePath & path) -> std::pair<EditablePath *, std::size_t>
    {
      for (std::size_t index = 0; index < path.segments.size(); ++index)
        if (path.segments[index].id == id)
          return {&path, index};
      return {nullptr, 0};
    };
    if (part.outer)
    {
      auto found = inspect(*part.outer);
      if (found.first)
        return found;
    }
    for (EditablePath & hole : part.holes)
    {
      auto found = inspect(hole);
      if (found.first)
        return found;
    }
  }
  return {nullptr, 0};
}

/// Создаёт точку и назначает ей новый идентификатор документа.
EditablePoint makePoint(EditablePolygonDocument & document, const EditorPointValue & value)
{
  return {document.allocateEntityId(), value.x, value.y};
}

/// Заполняет сегмент, назначая идентификаторы только используемым служебным точкам.
EditableSegment makeSegment(EditablePolygonDocument & document, const EditorSegmentValue & value)
{
  EditableSegment result;
  result.id = document.allocateEntityId();
  result.kind = value.kind;
  result.clockwise = value.clockwise;
  if (value.kind == EditableSegmentKind::Arc)
    result.center = makePoint(document, value.center);
  else if (value.kind == EditableSegmentKind::CubicBezier)
  {
    result.control1 = makePoint(document, value.control1);
    result.control2 = makePoint(document, value.control2);
  }
  return result;
}

/// Проверяет конечность всех чисел описания сегмента, которые участвуют в выбранном виде.
bool validSegmentValue(const EditorSegmentValue & value) noexcept
{
  if (value.kind == EditableSegmentKind::Arc)
    return finite(value.center.x) && finite(value.center.y);
  if (value.kind == EditableSegmentKind::CubicBezier)
    return finite(value.control1.x) && finite(value.control1.y) && finite(value.control2.x) && finite(value.control2.y);
  return true;
}

/// Назначает копии цепочки полностью новые идентификаторы без изменения её геометрии.
void remapPath(EditablePolygonDocument & document, EditablePath & path)
{
  path.id = document.allocateEntityId();
  for (EditablePoint & point : path.vertices)
    point.id = document.allocateEntityId();
  for (EditableSegment & segment : path.segments)
  {
    segment.id = document.allocateEntityId();
    visitSegmentPoints(segment, [&document](EditablePoint & point) { point.id = document.allocateEntityId(); });
  }
}

/// Применяет одну команду к временному документу и возвращает предметную ошибку без исключения.
bool applyCommand(EditablePolygonDocument & document, const EditorCommand & command, std::string & error)
{
  return std::visit(
    [&](const auto & value) -> bool
    {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, SetProblemIdCommand>)
        document.problemId = value.problemId;
      else if constexpr (std::is_same_v<T, SetSheetCommand>)
      {
        if (!finite(value.width) || !finite(value.height))
        {
          error = "Размеры листа должны быть конечными числами";
          return false;
        }
        document.sheet.width = value.width;
        document.sheet.height = value.height;
      }
      else if constexpr (std::is_same_v<T, SetManufacturingCommand>)
      {
        if (!finite(value.manufacturing.sheetMargin) || !finite(value.manufacturing.partSpacing) ||
            !finite(value.manufacturing.kerf) || !finite(value.manufacturing.curveTolerance))
        {
          error = "Производственные параметры должны быть конечными числами";
          return false;
        }
        document.manufacturing = value.manufacturing;
      }
      else if constexpr (std::is_same_v<T, AddPartCommand>)
      {
        EditablePart part;
        part.id = document.allocateEntityId();
        part.partId = value.partId;
        part.quantity = value.quantity;
        part.allowedRotations = value.allowedRotations;
        document.parts.push_back(std::move(part));
      }
      else if constexpr (std::is_same_v<T, DeletePartCommand>)
      {
        const auto found = std::find_if(document.parts.begin(), document.parts.end(),
                                        [&](const EditablePart & part) { return part.id == value.part; });
        if (found == document.parts.end())
        {
          error = "Тип детали больше не существует";
          return false;
        }
        document.parts.erase(found);
      }
      else if constexpr (std::is_same_v<T, DuplicatePartCommand>)
      {
        EditablePart * source = findPart(document, value.part);
        if (!source)
        {
          error = "Тип детали больше не существует";
          return false;
        }
        EditablePart copy = *source;
        copy.id = document.allocateEntityId();
        copy.partId = value.partId;
        if (copy.outer)
          remapPath(document, *copy.outer);
        for (EditablePath & hole : copy.holes)
          remapPath(document, hole);
        document.parts.push_back(std::move(copy));
      }
      else if constexpr (std::is_same_v<T, SetPartPropertiesCommand>)
      {
        EditablePart * part = findPart(document, value.part);
        if (!part)
        {
          error = "Тип детали больше не существует";
          return false;
        }
        part->partId = value.partId;
        part->quantity = value.quantity;
        part->allowedRotations = value.allowedRotations;
      }
      else if constexpr (std::is_same_v<T, CreatePathCommand>)
      {
        EditablePart * part = findPart(document, value.part);
        if (!part)
        {
          error = "Тип детали больше не существует";
          return false;
        }
        if (!finite(value.firstVertex.x) || !finite(value.firstVertex.y))
        {
          error = "Координаты должны быть конечными числами";
          return false;
        }
        if (!value.hole && part->outer)
        {
          error = "Внешний контур уже существует";
          return false;
        }
        EditablePath path;
        path.id = document.allocateEntityId();
        path.vertices.push_back(makePoint(document, value.firstVertex));
        if (value.hole)
          part->holes.push_back(std::move(path));
        else
          part->outer = std::move(path);
      }
      else if constexpr (std::is_same_v<T, DeletePathCommand>)
      {
        bool removed = false;
        for (EditablePart & part : document.parts)
        {
          if (part.outer && part.outer->id == value.path)
          {
            part.outer.reset();
            removed = true;
            break;
          }
          const auto hole =
            std::find_if(part.holes.begin(), part.holes.end(), [&](const EditablePath & path) { return path.id == value.path; });
          if (hole != part.holes.end())
          {
            part.holes.erase(hole);
            removed = true;
            break;
          }
        }
        if (!removed)
        {
          error = "Контур больше не существует";
          return false;
        }
      }
      else if constexpr (std::is_same_v<T, AppendSegmentCommand>)
      {
        EditablePath * path = findPath(document, value.path);
        if (!path)
        {
          error = "Контур больше не существует";
          return false;
        }
        if (path->closed || path->vertices.empty())
        {
          error = "Сегмент можно добавить только в непустую открытую цепочку";
          return false;
        }
        if (!finite(value.endVertex.x) || !finite(value.endVertex.y) || !validSegmentValue(value.segment))
        {
          error = "Параметры сегмента должны быть конечными числами";
          return false;
        }
        path->segments.push_back(makeSegment(document, value.segment));
        path->vertices.push_back(makePoint(document, value.endVertex));
      }
      else if constexpr (std::is_same_v<T, ClosePathCommand>)
      {
        EditablePath * path = findPath(document, value.path);
        if (!path)
        {
          error = "Контур больше не существует";
          return false;
        }
        if (path->closed || path->vertices.size() < 2 || path->segments.size() + 1 != path->vertices.size())
        {
          error = "Замкнуть можно только согласованную открытую цепочку";
          return false;
        }
        if (!validSegmentValue(value.segment))
        {
          error = "Параметры сегмента должны быть конечными числами";
          return false;
        }
        path->segments.push_back(makeSegment(document, value.segment));
        path->closed = true;
      }
      else if constexpr (std::is_same_v<T, OpenPathCommand>)
      {
        EditablePath * path = findPath(document, value.path);
        if (!path || !path->closed || path->segments.size() != path->vertices.size())
        {
          error = "Контур не является согласованной замкнутой цепочкой";
          return false;
        }
        path->segments.pop_back();
        path->closed = false;
      }
      else if constexpr (std::is_same_v<T, UpdateSegmentCommand>)
      {
        if (!validSegmentValue(value.value))
        {
          error = "Параметры сегмента должны быть конечными числами";
          return false;
        }
        auto [path, index] = findSegment(document, value.segment);
        if (!path)
        {
          error = "Сегмент больше не существует";
          return false;
        }
        EditableSegment replacement = makeSegment(document, value.value);
        replacement.id = path->segments[index].id;
        if (replacement.kind == path->segments[index].kind)
        {
          if (replacement.kind == EditableSegmentKind::Arc)
            replacement.center.id = path->segments[index].center.id;
          if (replacement.kind == EditableSegmentKind::CubicBezier)
          {
            replacement.control1.id = path->segments[index].control1.id;
            replacement.control2.id = path->segments[index].control2.id;
          }
        }
        path->segments[index] = replacement;
      }
      else if constexpr (std::is_same_v<T, DeleteSegmentCommand>)
      {
        auto [path, index] = findSegment(document, value.segment);
        if (!path || path->vertices.size() < 2)
        {
          error = "Сегмент больше не существует";
          return false;
        }
        if (path->closed)
        {
          const std::size_t vertexCount = path->vertices.size();
          path->segments.erase(path->segments.begin() + static_cast<std::ptrdiff_t>(index));

          // После разрыва цепочка начинается с бывшего конца удалённого сегмента.
          // Так оставшиеся сегменты сохраняют исходные начальные и конечные вершины.
          const std::size_t vertexStart = (index + 1) % vertexCount;
          std::rotate(path->vertices.begin(), path->vertices.begin() + static_cast<std::ptrdiff_t>(vertexStart),
                      path->vertices.end());
          const std::size_t segmentStart = std::min(index, path->segments.size());
          std::rotate(path->segments.begin(), path->segments.begin() + static_cast<std::ptrdiff_t>(segmentStart),
                      path->segments.end());
          path->closed = false;
          return true;
        }
        path->segments.erase(path->segments.begin() + static_cast<std::ptrdiff_t>(index));
        path->vertices.erase(path->vertices.begin() + static_cast<std::ptrdiff_t>(index + 1));
      }
      else if constexpr (std::is_same_v<T, MovePointCommand>)
      {
        EditablePoint * point = findPoint(document, value.point);
        if (!point)
        {
          error = "Точка больше не существует";
          return false;
        }
        if (!finite(value.value.x) || !finite(value.value.y))
        {
          error = "Координаты должны быть конечными числами";
          return false;
        }
        point->x = value.value.x;
        point->y = value.value.y;
      }
      else if constexpr (std::is_same_v<T, TranslatePointsCommand>)
      {
        if (!finite(value.deltaX) || !finite(value.deltaY))
        {
          error = "Смещение должно быть конечным числом";
          return false;
        }
        std::set<std::uint64_t> unique;
        std::vector<EditablePoint *> points;
        for (EntityId id : value.points)
        {
          if (!unique.insert(id.value).second)
          {
            error = "Точка указана для переноса повторно";
            return false;
          }
          EditablePoint * point = findPoint(document, id);
          if (!point)
          {
            error = "Точка больше не существует";
            return false;
          }
          if (!finite(point->x + value.deltaX) || !finite(point->y + value.deltaY))
          {
            error = "Результат переноса выходит за числовой диапазон";
            return false;
          }
          points.push_back(point);
        }
        for (EditablePoint * point : points)
        {
          point->x += value.deltaX;
          point->y += value.deltaY;
        }
      }
      return true;
    },
    command);
}
} // namespace

/// Создаёт начальную чистую сессию и сохраняет верхнюю границу идентификаторов документа.
PolygonEditorSession::PolygonEditorSession()
  : PolygonEditorSession(EditablePolygonDocument{}, true)
{
}

/// Принимает документ по значению, чтобы история не делила изменяемое состояние с вызывающим кодом.
PolygonEditorSession::PolygonEditorSession(EditablePolygonDocument document, bool clean)
{
  reset(std::move(document), clean);
}

/// Возвращает ссылку, действительную до следующей изменяющей операции сессии.
const EditablePolygonDocument & PolygonEditorSession::document() const noexcept
{
  return document_;
}

/// Вычисляет подписи непосредственно из текущей позиции, не сохраняя производное состояние.
EditorHistoryState PolygonEditorSession::history() const
{
  EditorHistoryState result;
  result.canUndo = position_ > 0;
  result.canRedo = position_ < entries_.size();
  result.revision = currentRevision_;
  result.dirty = !cleanRevision_ || *cleanRevision_ != currentRevision_;
  if (result.canUndo)
    result.undoLabel = entries_[position_ - 1].label;
  if (result.canRedo)
    result.redoLabel = entries_[position_].label;
  return result;
}

/// Применяет команды к копии и публикует её только после полного успеха всех операций.
EditorCommandResult PolygonEditorSession::execute(const EditorCommandBatch & batch)
{
  EditorCommandResult result;
  if (batch.commands.empty())
  {
    result.error = "Пакет изменений пуст";
    result.history = history();
    return result;
  }
  EditablePolygonDocument candidate = document_;
  try
  {
    for (const EditorCommand & command : batch.commands)
      if (!applyCommand(candidate, command, result.error))
      {
        result.history = history();
        return result;
      }
  }
  catch (const std::exception & exception)
  {
    result.error = std::string("Изменение документа не выполнено: ") + exception.what();
    result.history = history();
    return result;
  }

  if (position_ < entries_.size())
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(position_), entries_.end());
  const std::uint64_t next = nextRevision_++;
  const bool coalesce = batch.gestureId && position_ > 0 && position_ == entries_.size() &&
                        entries_.back().gestureId == batch.gestureId && finishedGesture_ != batch.gestureId;
  if (coalesce)
  {
    entries_.back().after = candidate;
    entries_.back().afterRevision = next;
    if (!batch.label.empty())
      entries_.back().label = batch.label;
  }
  else
  {
    entries_.push_back(
      {batch.label.empty() ? "Изменение документа" : batch.label, document_, candidate, currentRevision_, next, batch.gestureId});
    ++position_;
  }
  document_ = std::move(candidate);
  currentRevision_ = next;
  identifierHighWater_ = std::max(identifierHighWater_, document_.nextEntityId());
  if (entries_.size() > MAX_HISTORY_ENTRIES)
  {
    entries_.erase(entries_.begin());
    --position_;
  }
  result.accepted = true;
  result.diagnostics = validateEditableDocument(document_);
  result.history = history();
  return result;
}

/// Восстанавливает снимок до транзакции, не уменьшая монотонный источник идентификаторов.
EditorCommandResult PolygonEditorSession::undo()
{
  EditorCommandResult result;
  if (position_ == 0)
  {
    result.error = "Нет изменений для отмены";
    result.history = history();
    return result;
  }
  const HistoryEntry & entry = entries_[position_ - 1];
  document_ = entry.before;
  document_.restoreNextEntityId(identifierHighWater_);
  currentRevision_ = entry.beforeRevision;
  --position_;
  result.accepted = true;
  result.diagnostics = validateEditableDocument(document_);
  result.history = history();
  return result;
}

/// Восстанавливает сохранённый итог транзакции, сохраняя глобальную верхнюю границу идентификаторов.
EditorCommandResult PolygonEditorSession::redo()
{
  EditorCommandResult result;
  if (position_ >= entries_.size())
  {
    result.error = "Нет изменений для повтора";
    result.history = history();
    return result;
  }
  const HistoryEntry & entry = entries_[position_];
  document_ = entry.after;
  document_.restoreNextEntityId(identifierHighWater_);
  currentRevision_ = entry.afterRevision;
  ++position_;
  result.accepted = true;
  result.diagnostics = validateEditableDocument(document_);
  result.history = history();
  return result;
}

/// Выдаёт отдельный идентификатор жеста; ноль обозначает исчерпание служебного диапазона.
std::uint64_t PolygonEditorSession::beginGesture() noexcept
{
  if (nextGestureId_ == 0 || nextGestureId_ == std::numeric_limits<std::uint64_t>::max())
    return 0;
  return nextGestureId_++;
}

/// Удаляет последнюю объединённую транзакцию только пока после неё нет других действий.
EditorCommandResult PolygonEditorSession::cancelGesture(std::uint64_t gestureId)
{
  EditorCommandResult result;
  if (position_ == 0 || position_ != entries_.size() || entries_.back().gestureId != gestureId)
  {
    result.error = "Объединённое изменение больше нельзя отменить";
    result.history = history();
    return result;
  }
  document_ = entries_.back().before;
  document_.restoreNextEntityId(identifierHighWater_);
  currentRevision_ = entries_.back().beforeRevision;
  entries_.pop_back();
  --position_;
  result.accepted = true;
  result.diagnostics = validateEditableDocument(document_);
  result.history = history();
  return result;
}

/// Запрещает объединять следующие команды с уже завершённым жестом.
void PolygonEditorSession::finishGesture(std::uint64_t gestureId) noexcept
{
  finishedGesture_ = gestureId;
}

/// Запоминает текущую редакцию; сохранение снимка истории при этом не требуется.
void PolygonEditorSession::markSaved() noexcept
{
  cleanRevision_ = currentRevision_;
}

/// Начинает независимую историю и сохраняет следующий идентификатор как глобальную верхнюю границу.
void PolygonEditorSession::reset(EditablePolygonDocument document, bool clean)
{
  document_ = std::move(document);
  entries_.clear();
  position_ = 0;
  currentRevision_ = nextRevision_++;
  cleanRevision_ = clean ? std::optional<std::uint64_t>{currentRevision_} : std::nullopt;
  identifierHighWater_ = document_.nextEntityId();
  nextGestureId_ = 1;
  finishedGesture_.reset();
}
} // namespace aipackaging::editor
