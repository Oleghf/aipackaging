#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_MODEL_JOBS_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_MODEL_JOBS_H

#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <polygon_artifact_store.h>

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Загружает внешний комплект ONNX и регистрирует только полностью проверенную модель.
class LocalPolygonModelGateway final : public IPolygonModelGateway
{
public:
  /// Связывает шлюз модели с общим процессным хранилищем.
  explicit LocalPolygonModelGateway(std::shared_ptr<PolygonArtifactStore> store);
  /// Проверяет каталог модели и возвращает её идентичность.
  PolygonModelLoadResult load(const std::string & directory) override;
  /// Без исключений освобождает зарегистрированную модель.
  void release(PolygonModelHandle model) noexcept override;

private:
  std::shared_ptr<PolygonArtifactStore> store_;
};

/// Проверяет комплект модели в отдельном потоке и доставляет события приложению.
class StdThreadPolygonModelJobRunner final : public IPolygonModelJobRunner
{
public:
  /// Сохраняет шлюз модели и диспетчер потока приложения.
  StdThreadPolygonModelJobRunner(std::shared_ptr<IPolygonModelGateway> gateway,
                                 std::shared_ptr<IApplicationDispatcher> dispatcher);
  /// Запрашивает остановку и присоединяет рабочий поток.
  ~StdThreadPolygonModelJobRunner() override;
  /// Запускает одну проверку, если другая проверка не выполняется.
  std::optional<PolygonModelJobHandle> start(const std::string & directory, PolygonModelJobCallbacks callbacks,
                                             std::string & error) override;
  /// Запрашивает остановку совпадающей проверки.
  void cancel(PolygonModelJobHandle job) noexcept override;
  /// Передаёт освобождение модели нижележащему шлюзу.
  void release(PolygonModelHandle model) noexcept override;

private:
  std::shared_ptr<IPolygonModelGateway> gateway_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::mutex mutex_;
  std::jthread worker_;
  std::uint64_t nextJob_ = 1;
  std::optional<PolygonModelJobHandle> activeJob_;
};
#endif

#endif
