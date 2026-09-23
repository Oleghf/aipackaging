#ifndef AIPACKAGING_APPLICATION_ACTIVEPOLYGONDOCUMENT_H
#define AIPACKAGING_APPLICATION_ACTIVEPOLYGONDOCUMENT_H

#include <cstdint>
#include <optional>
#include <string>

#include <polygonidentifiers.h>

/// Обозначает происхождение активного документа без раскрытия формата источника.
enum class PolygonDocumentSource : std::uint8_t
{
  None,
  ProblemFile,
  Draft,
  Imported
};

/// Описывает согласованное состояние одного активного полигонального документа.
struct ActivePolygonDocumentState
{
  std::optional<PolygonDocumentHandle> handle;
  PolygonDocumentSource source = PolygonDocumentSource::None;
  std::string sourceIdentifier;
  bool dirty = false;
  bool valid = false;
  bool running = false;
  bool hasSolution = false;
  bool solutionStale = false;
};

/// Владеет признаками жизненного цикла одного документа независимо от Qt и решателя.
class ActivePolygonDocument
{
public:
  /// Возвращает неизменяемое согласованное состояние документа.
  const ActivePolygonDocumentState & state() const noexcept;
  /// Заменяет документ, если фоновая работа уже завершена.
  bool replace(PolygonDocumentHandle handle, PolygonDocumentSource source, std::string sourceIdentifier, bool valid);
  /// Удаляет активный документ, если фоновая работа уже завершена.
  bool clear() noexcept;
  /// Отмечает изменение документа и устаревание существующего решения.
  void markChanged(bool valid) noexcept;
  /// Отмечает успешное сохранение документа в указанном источнике.
  void markSaved(PolygonDocumentSource source, std::string sourceIdentifier);
  /// Обновляет результат предметной проверки документа.
  void setValid(bool valid) noexcept;
  /// Начинает работу только для существующего корректного документа.
  bool beginRun() noexcept;
  /// Завершает текущую работу и отмечает наличие нового проверенного решения.
  void finishRun(bool hasSolution) noexcept;
  /// Удаляет сведения о доступном решении без изменения документа.
  void clearSolution() noexcept;

private:
  ActivePolygonDocumentState state_;
};

#endif
