#include <utility>

#include <activepolygondocument.h>

/// Возвращает текущее состояние без создания отдельной копии.
const ActivePolygonDocumentState & ActivePolygonDocument::state() const noexcept
{
  return state_;
}

/// Полностью публикует новый документ только после проверки отсутствия активной работы.
bool ActivePolygonDocument::replace(PolygonDocumentHandle handle, PolygonDocumentSource source, std::string sourceIdentifier,
                                    bool valid)
{
  if (state_.running || !handle || source == PolygonDocumentSource::None)
    return false;

  ActivePolygonDocumentState next;
  next.handle = handle;
  next.source = source;
  next.sourceIdentifier = std::move(sourceIdentifier);
  next.valid = valid;
  state_ = std::move(next);
  return true;
}

/// Сбрасывает все признаки документа только в состоянии без фоновой работы.
bool ActivePolygonDocument::clear() noexcept
{
  if (state_.running)
    return false;
  state_ = {};
  return true;
}

/// Фиксирует пользовательское изменение и сохраняет факт устаревшего результата.
void ActivePolygonDocument::markChanged(bool valid) noexcept
{
  if (!state_.handle || state_.running)
    return;
  state_.dirty = true;
  state_.valid = valid;
  state_.solutionStale = state_.solutionStale || state_.hasSolution;
  state_.hasSolution = false;
}

/// Обновляет происхождение документа и устанавливает чистую точку сохранения.
void ActivePolygonDocument::markSaved(PolygonDocumentSource source, std::string sourceIdentifier)
{
  if (!state_.handle || state_.running || source == PolygonDocumentSource::None)
    return;
  state_.source = source;
  state_.sourceIdentifier = std::move(sourceIdentifier);
  state_.dirty = false;
}

/// Запоминает итог проверки, не изменяя содержимое и источник документа.
void ActivePolygonDocument::setValid(bool valid) noexcept
{
  if (state_.handle && !state_.running)
    state_.valid = valid;
}

/// Проверяет предусловия запуска и атомарно устанавливает признак фоновой работы.
bool ActivePolygonDocument::beginRun() noexcept
{
  if (!state_.handle || !state_.valid || state_.running)
    return false;
  state_.running = true;
  return true;
}

/// Снимает признак работы и заменяет сведения о результате данными завершённого запуска.
void ActivePolygonDocument::finishRun(bool hasSolution) noexcept
{
  if (!state_.running)
    return;
  state_.running = false;
  state_.hasSolution = hasSolution;
  if (hasSolution)
    state_.solutionStale = false;
}

/// Очищает доступность результата, сохраняя признак устаревания после изменения документа.
void ActivePolygonDocument::clearSolution() noexcept
{
  state_.hasSolution = false;
}
