#include <limits>
#include <stdexcept>

#include <aipackaging/editor/polygon_document.h>

namespace aipackaging::editor
{
/// Назначает листу первый устойчивый идентификатор документа.
EditablePolygonDocument::EditablePolygonDocument()
{
  sheet.id = allocateEntityId();
}

/// Возвращает текущее значение и сдвигает источник без возможности повторного использования.
EntityId EditablePolygonDocument::allocateEntityId()
{
  if (nextEntityId_ == 0 || nextEntityId_ == std::numeric_limits<std::uint64_t>::max())
    throw std::overflow_error("исчерпан диапазон идентификаторов редактируемого документа");
  return EntityId{nextEntityId_++};
}

/// Читает значение, которое будет выдано следующей создаваемой сущности.
std::uint64_t EditablePolygonDocument::nextEntityId() const noexcept
{
  return nextEntityId_;
}

/// Принимает восстановленное значение только как монотонное продвижение источника.
bool EditablePolygonDocument::restoreNextEntityId(std::uint64_t value) noexcept
{
  if (value == 0 || value < nextEntityId_)
    return false;
  nextEntityId_ = value;
  return true;
}
} // namespace aipackaging::editor
