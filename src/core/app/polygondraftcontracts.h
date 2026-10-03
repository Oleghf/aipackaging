#ifndef AIPACKAGING_APPLICATION_POLYGONDRAFTCONTRACTS_H
#define AIPACKAGING_APPLICATION_POLYGONDRAFTCONTRACTS_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <polygoneditablecontracts.h>

/// Идентифицирует одну поставленную в очередь операцию автоматического черновика.
struct PolygonDraftJobHandle
{
  std::uint64_t value = 0;

  /// Сравнивает процессные идентификаторы работ.
  bool operator==(const PolygonDraftJobHandle &) const = default;
};

/// Содержит неизменяемый снимок и происхождение одного поколения автоматического черновика.
struct PolygonDraftSaveRequest
{
  std::string filePath;
  std::shared_ptr<const aipackaging::editor::EditablePolygonDocument> document;
  PolygonDocumentSource source = PolygonDocumentSource::None;
  std::string sourceIdentifier;
  std::uint64_t generation = 0;
  std::optional<PolygonSourceFingerprint> baseFingerprint;
  std::uint64_t documentIdentity = 0;
  std::optional<PolygonSourceFingerprint> recoveryFingerprint;
};

/// Доставляет итог одной записи или удаления в поток владельца контроллера.
using PolygonDraftJobCallback = std::function<void(PolygonDraftJobHandle, std::uint64_t, const PolygonDocumentOperationResult &)>;

/// Задаёт последовательное удаление устаревшего автоматического черновика.
struct PolygonDraftInvalidationRequest
{
  std::string filePath;
  std::uint64_t generation = 0;
  std::uint64_t documentIdentity = 0;
  std::optional<PolygonSourceFingerprint> recoveryFingerprint;
};

/// Последовательно записывает поколения автоматического черновика вне потока интерфейса.
class IPolygonDraftJobRunner
{
public:
  /// Обеспечивает корректное уничтожение реализации через интерфейс.
  virtual ~IPolygonDraftJobRunner() = default;
  /// Заменяет ожидающий запрос и возвращает идентификатор принятой работы.
  virtual std::optional<PolygonDraftJobHandle> submit(PolygonDraftSaveRequest request, PolygonDraftJobCallback callback,
                                                      std::string & error) = 0;
  /// Удаляет принадлежащий документу черновик после активной записи; последующие записи и чужой файл сохраняются.
  virtual std::optional<PolygonDraftJobHandle> invalidate(PolygonDraftInvalidationRequest request,
                                                          PolygonDraftJobCallback callback, std::string & error) = 0;
  /// Без исключений ожидает завершения принятых записей и инвалидирований без ожидания доставки событий.
  virtual void flush() noexcept = 0;
};

#endif
