#ifndef AIPACKAGING_INFRASTRUCTURE_BACKGROUND_EVENT_DELIVERY_H
#define AIPACKAGING_INFRASTRUCTURE_BACKGROUND_EVENT_DELIVERY_H

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <polygonworkspaceview.h>

namespace aipackaging::desktop::detail
{
/// Создаёт функцию события внутри защищённой области и без исключений передаёт её диспетчеру.
template<typename Factory>
bool postSafely(const std::shared_ptr<IApplicationDispatcher> & dispatcher, Factory && factory) noexcept
{
  try
  {
    if (!dispatcher)
      return false;
    std::function<void()> callback = std::forward<Factory>(factory)();
    return dispatcher->post(std::move(callback));
  }
  catch (...)
  {
    return false;
  }
}

/// Без исключений освобождает идентификатор завершённой работы перед публикацией результата.
template<typename Handle>
void clearActiveJob(std::mutex & mutex, std::optional<Handle> & activeJob, Handle job) noexcept
{
  try
  {
    std::lock_guard lock(mutex);
    if (activeJob == job)
      activeJob.reset();
  }
  catch (...)
  {
    return;
  }
}

/// Удерживает проверенное решение до подтверждённой передачи прикладному контроллеру.
class PendingNestingDelivery
{
public:
  /// Копирует результат и запоминает порт освобождения его процессного идентификатора.
  PendingNestingDelivery(std::shared_ptr<IPolygonDocumentGateway> documents, const NestingRunResult & result)
    : documents_(std::move(documents))
    , result_(result)
  {
  }

  /// Освобождает решение, если получатель не подтвердил принятие владения.
  ~PendingNestingDelivery()
  {
    if (ownsResult_ && result_.solution && documents_)
      documents_->release(*result_.solution);
  }

  /// Возвращает неизменяемый результат для передачи функции завершения.
  const NestingRunResult & result() const noexcept { return result_; }
  /// Передаёт ответственность за идентификатор успешно завершившемуся обработчику.
  void commit() noexcept { ownsResult_ = false; }

private:
  std::shared_ptr<IPolygonDocumentGateway> documents_;
  NestingRunResult result_;
  bool ownsResult_ = true;
};

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Удерживает загруженную модель до подтверждённой передачи прикладному контроллеру.
class PendingModelDelivery
{
public:
  /// Копирует результат загрузки и запоминает шлюз освобождения модели.
  PendingModelDelivery(std::shared_ptr<IPolygonModelGateway> gateway, const PolygonModelLoadResult & result)
    : gateway_(std::move(gateway))
    , result_(result)
  {
  }

  /// Освобождает модель, если получатель не подтвердил принятие владения.
  ~PendingModelDelivery()
  {
    if (ownsResult_ && result_.success && result_.model && gateway_)
      gateway_->release(result_.model);
  }

  /// Возвращает неизменяемый результат для передачи функции завершения.
  const PolygonModelLoadResult & result() const noexcept { return result_; }
  /// Передаёт ответственность за идентификатор успешно завершившемуся обработчику.
  void commit() noexcept { ownsResult_ = false; }

private:
  std::shared_ptr<IPolygonModelGateway> gateway_;
  PolygonModelLoadResult result_;
  bool ownsResult_ = true;
};
#endif
} // namespace aipackaging::desktop::detail

#endif
