#include <stdexcept>
#include <utility>

#include <polygon_model_jobs.h>

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
#include <aipackaging/inference/polygon_onnx.h>

#include "background_event_delivery.h"

using namespace aipackaging::desktop::detail;

/// Сохраняет хранилище, в котором регистрируются проверенные комплекты модели.
LocalPolygonModelGateway::LocalPolygonModelGateway(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Проверяет комплект через ONNX-адаптер и публикует его только после полного успеха.
PolygonModelLoadResult LocalPolygonModelGateway::load(const std::string & directory)
{
  std::string error;
  std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model =
    aipackaging::inference::PolygonOnnxPolicy::Load(directory, error);
  if (!model)
    return {false, error};
  PolygonModelLoadResult result;
  result.success = true;
  result.modelId = model->metadata().modelId;
  result.modelSha256 = model->metadata().modelSha256;
  result.model = store_->addModel(std::move(model));
  return result;
}

/// Передаёт освобождение модели общему процессному хранилищу.
void LocalPolygonModelGateway::release(PolygonModelHandle model) noexcept
{
  store_->release(model);
}

/// Сохраняет шлюз и диспетчер; поток создаётся только при первой проверке.
StdThreadPolygonModelJobRunner::StdThreadPolygonModelJobRunner(std::shared_ptr<IPolygonModelGateway> gateway,
                                                               std::shared_ptr<IApplicationDispatcher> dispatcher)
  : gateway_(std::move(gateway))
  , dispatcher_(std::move(dispatcher))
{
}

/// Останавливает активную проверку и не оставляет поток после уничтожения адаптера.
StdThreadPolygonModelJobRunner::~StdThreadPolygonModelJobRunner()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    worker_.join();
  }
}

/// Завершает прежний поток и выполняет одну проверку с отложенной доставкой результата.
std::optional<PolygonModelJobHandle>
StdThreadPolygonModelJobRunner::start(const std::string & directory, PolygonModelJobCallbacks callbacks, std::string & error)
{
  std::lock_guard lock(mutex_);
  if (activeJob_)
  {
    error = "Другая модель уже проверяется";
    return std::nullopt;
  }
  if (worker_.joinable())
    worker_.join();
  std::string directoryCopy = directory;
  const PolygonModelJobHandle job{nextJob_++};
  activeJob_ = job;
  const auto gateway = gateway_;
  const auto dispatcher = dispatcher_;
  try
  {
    worker_ = std::jthread(
      [this, gateway, dispatcher, directory = std::move(directoryCopy), callbacks = std::move(callbacks),
       job](const std::stop_token & stopToken) mutable noexcept
      {
        PolygonModelLoadResult result;
        try
        {
          std::string failure;
          try
          {
            result = gateway->load(directory);
            if (!result.success)
              failure = result.error;
          }
          catch (const std::exception & exception)
          {
            failure = exception.what();
          }
          catch (...)
          {
            failure = "Проверка модели завершилась неизвестной ошибкой";
          }
          const bool cancelled = stopToken.stop_requested();
          if (cancelled && result.success && result.model)
          {
            gateway->release(result.model);
            result = {};
          }
          clearActiveJob(mutex_, activeJob_, job);
          if (cancelled && callbacks.cancelled)
          {
            postSafely(dispatcher,
                       [&callbacks, job]()
                       {
                         return [callback = std::move(callbacks.cancelled), job]() noexcept
                         {
                           try
                           {
                             callback(job);
                           }
                           catch (...)
                           {
                             // Ошибка пользовательского обработчика не должна завершать цикл доставки.
                             return;
                           }
                         };
                       });
          }
          else if (result.success)
          {
            try
            {
              auto pending = std::make_shared<PendingModelDelivery>(gateway, result);
              result = {};
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
                                 // RAII-оболочка освободит модель после ошибки обработчика.
                                 return;
                               }
                             };
                           });
              }
            }
            catch (...)
            {
              if (result.success && result.model)
                gateway->release(result.model);
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
          if (result.success && result.model)
            gateway->release(result.model);
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
    error = "Не удалось создать рабочий поток проверки модели";
    return std::nullopt;
  }
  return job;
}

/// Запрашивает остановку только совпадающей актуальной проверки.
void StdThreadPolygonModelJobRunner::cancel(PolygonModelJobHandle job) noexcept
{
  std::lock_guard lock(mutex_);
  if (activeJob_ == job && worker_.joinable())
    worker_.request_stop();
}

/// Передаёт освобождение модели потокобезопасному шлюзу.
void StdThreadPolygonModelJobRunner::release(PolygonModelHandle model) noexcept
{
  gateway_->release(model);
}
#endif
