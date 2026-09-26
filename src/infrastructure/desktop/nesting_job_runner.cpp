#include <chrono>
#include <stdexcept>
#include <utility>

#include <nesting_job_runner.h>

#include "background_event_delivery.h"

using namespace aipackaging::desktop::detail;

/// Сохраняет зависимости; поток создаётся только при первом запуске.
StdThreadNestingJobRunner::StdThreadNestingJobRunner(std::shared_ptr<IPolygonNestingBackend> backend,
                                                     std::shared_ptr<IApplicationDispatcher> dispatcher,
                                                     std::shared_ptr<IPolygonDocumentGateway> documents)
  : backend_(std::move(backend))
  , dispatcher_(std::move(dispatcher))
  , documents_(std::move(documents))
{
}

/// Совместно останавливает активную работу и присоединяет поток до уничтожения зависимостей.
StdThreadNestingJobRunner::~StdThreadNestingJobRunner()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    worker_.join();
  }
}

/// Завершает предыдущий поток, создаёт идентификатор и запускает новую работу.
std::optional<NestingJobHandle> StdThreadNestingJobRunner::start(PolygonDocumentHandle document,
                                                                 const NestingRunRequest & request, NestingJobCallbacks callbacks,
                                                                 std::string & error)
{
  std::lock_guard lock(mutex_);
  if (activeJob_)
  {
    error = "Другая работа уже выполняется";
    return std::nullopt;
  }
  if (worker_.joinable())
    worker_.join();
  NestingRunRequest requestCopy = request;
  const NestingJobHandle job{nextJob_++};
  activeJob_ = job;
  const auto backend = backend_;
  const auto dispatcher = dispatcher_;
  const auto documents = documents_;
  try
  {
    worker_ = std::jthread(
      [this, backend, dispatcher, documents, document, request = requestCopy, callbacks = std::move(callbacks),
       job](const std::stop_token & stopToken) mutable noexcept
      {
        std::optional<NestingRunResult> result;
        try
        {
          auto lastProgress = std::chrono::steady_clock::time_point::min();
          IPolygonNestingBackend::Control control;
          control.cancellationRequested = [stopToken]()
          {
            return stopToken.stop_requested();
          };
          control.progress = [dispatcher, callbacks, job, &lastProgress](const NestingProgress & progress) mutable
          {
            const auto now = std::chrono::steady_clock::now();
            const bool finalUpdate = progress.total > 0 && progress.completed >= progress.total;
            if (!finalUpdate && lastProgress != std::chrono::steady_clock::time_point::min() &&
                now - lastProgress < std::chrono::milliseconds(100))
              return;
            lastProgress = now;
            if (callbacks.progress)
            {
              postSafely(dispatcher,
                         [&callbacks, job, progress]()
                         {
                           return [callback = callbacks.progress, job, progress]() noexcept
                           {
                             try
                             {
                               callback(job, progress);
                             }
                             catch (...)
                             {
                               // Отказ необязательного уведомления не влияет на вычисление результата.
                               return;
                             }
                           };
                         });
            }
          };
          std::string failure;
          try
          {
            result = backend->run(document, request, control);
          }
          catch (const std::exception & exception)
          {
            failure = exception.what();
          }
          catch (...)
          {
            failure = "Внутренняя реализация завершилась неизвестной ошибкой";
          }
          clearActiveJob(mutex_, activeJob_, job);
          // К моменту асинхронной публикации завершённая работа больше не блокирует новый запуск.
          if (result)
          {
            try
            {
              auto pending = std::make_shared<PendingNestingDelivery>(documents, *result);
              result.reset();
              if (callbacks.completed)
              {
                postSafely(dispatcher,
                           [&callbacks, pending, job]()
                           {
                             return [callback = std::move(callbacks.completed), pending, job]() noexcept
                             {
                               try
                               {
                                 if (callback(job, pending->result()))
                                   pending->commit();
                               }
                               catch (...)
                               {
                                 // RAII-оболочка освободит решение после ошибки обработчика.
                                 return;
                               }
                             };
                           });
              }
            }
            catch (...)
            {
              if (result && result->solution && documents)
                documents->release(*result->solution);
            }
          }
          else if (callbacks.failed)
          {
            postSafely(dispatcher,
                       [&callbacks, &failure, job]()
                       {
                         return [callback = std::move(callbacks.failed), job, failure]() noexcept
                         {
                           try
                           {
                             callback(job, failure);
                           }
                           catch (...)
                           {
                             // Повторная доставка ошибки могла бы создать бесконечный цикл отказов.
                             return;
                           }
                         };
                       });
          }
        }
        catch (...)
        {
          if (result && result->solution && documents)
            documents->release(*result->solution);
          clearActiveJob(mutex_, activeJob_, job);
        }
      });
  }
  catch (const std::exception & exception)
  {
    activeJob_.reset();
    error = exception.what();
    return std::nullopt;
  }
  catch (...)
  {
    activeJob_.reset();
    error = "Не удалось создать рабочий поток раскроя";
    return std::nullopt;
  }
  return job;
}

/// Сверяет идентификатор под блокировкой и запрашивает остановку соответствующего потока.
void StdThreadNestingJobRunner::cancel(NestingJobHandle job) noexcept
{
  std::lock_guard lock(mutex_);
  if (activeJob_ == job && worker_.joinable())
    worker_.request_stop();
}
