#include <exception>
#include <utility>

#include <polygon_draft_jobs.h>

#include "background_event_delivery.h"

using namespace aipackaging::desktop::detail;

/// Создаёт один долгоживущий поток, чтобы поколения всегда записывались последовательно.
StdThreadPolygonDraftJobRunner::StdThreadPolygonDraftJobRunner(std::shared_ptr<IPolygonEditableDocumentGateway> gateway,
                                                               std::shared_ptr<IApplicationDispatcher> dispatcher)
  : gateway_(std::move(gateway))
  , dispatcher_(std::move(dispatcher))
  , worker_([this](const std::stop_token & stopToken) noexcept { run(stopToken); })
{
}

/// Сначала дожидается принятой работы, затем останавливает пустой рабочий цикл.
StdThreadPolygonDraftJobRunner::~StdThreadPolygonDraftJobRunner()
{
  flush();
  worker_.request_stop();
  condition_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

/// Подменяет только ожидающую работу; уже начатая атомарная запись не прерывается.
std::optional<PolygonDraftJobHandle> StdThreadPolygonDraftJobRunner::submit(PolygonDraftSaveRequest request,
                                                                            PolygonDraftJobCallback callback, std::string & error)
{
  if (!gateway_ || !dispatcher_ || !request.document || request.filePath.empty() || request.generation == 0)
  {
    error = "Запрос автосохранения неполон";
    return std::nullopt;
  }
  try
  {
    std::lock_guard lock(mutex_);
    if (nextJob_ == 0)
    {
      error = "Исчерпан диапазон идентификаторов автосохранения";
      return std::nullopt;
    }
    const PolygonDraftJobHandle handle{nextJob_++};
    auto pending = std::make_shared<PendingJob>(PendingJob{handle, std::move(request), std::move(callback)});
    pending_ = std::move(pending);
    condition_.notify_one();
    return handle;
  }
  catch (const std::exception & exception)
  {
    error = exception.what();
  }
  catch (...)
  {
    error = "Не удалось поставить автосохранение в очередь";
  }
  return std::nullopt;
}

/// Ждёт состояния без активной и ожидающей записи; ошибки синхронизации не выходят наружу.
void StdThreadPolygonDraftJobRunner::flush() noexcept
{
  try
  {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this]() { return !active_ && !pending_; });
  }
  catch (...)
  {
    return;
  }
}

/// Выполняет файловую операцию без удержания блокировки и доставляет только итоговое событие.
void StdThreadPolygonDraftJobRunner::run(const std::stop_token & stopToken) noexcept
{
  try
  {
    runLoop(stopToken);
  }
  catch (...)
  {
    abandonJobs();
  }
}

/// Извлекает работу невыбрасывающим перемещением указателя и обрабатывает её вне блокировки.
void StdThreadPolygonDraftJobRunner::runLoop(const std::stop_token & stopToken)
{
  while (true)
  {
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, [this, &stopToken]() { return stopToken.stop_requested() || pending_; });
      if (stopToken.stop_requested() && !pending_)
        return;
    }

    std::shared_ptr<PendingJob> job;
    {
      std::lock_guard lock(mutex_);
      job = std::move(pending_);
      active_ = true;
    }

    PolygonDocumentOperationResult result;
    try
    {
      const PolygonDraftSaveRequest & request = job->request;
      result = gateway_->saveDraft(request.filePath, *request.document, request.source, request.sourceIdentifier,
                                   request.generation, request.baseFingerprint);
    }
    catch (const std::exception & exception)
    {
      result.error = exception.what();
    }
    catch (...)
    {
      result.error = "Автосохранение завершилось неизвестной ошибкой";
    }

    {
      std::lock_guard lock(mutex_);
      active_ = false;
    }
    condition_.notify_all();
    if (job->callback)
    {
      postSafely(dispatcher_,
                 [&job, &result]()
                 {
                   return [callback = std::move(job->callback), handle = job->handle, generation = job->request.generation,
                           result]() noexcept
                   {
                     try
                     {
                       callback(handle, generation, result);
                     }
                     catch (...)
                     {
                       return;
                     }
                   };
                 });
    }
  }
}

/// Удаляет недоставимые задания после внутреннего отказа, чтобы завершение не ожидало их бесконечно.
void StdThreadPolygonDraftJobRunner::abandonJobs() noexcept
{
  try
  {
    std::lock_guard lock(mutex_);
    active_ = false;
    pending_.reset();
  }
  catch (...)
  {
    // Повреждённый примитив синхронизации уже не позволяет безопасно менять состояние.
    (void)0;
  }
  condition_.notify_all();
}
