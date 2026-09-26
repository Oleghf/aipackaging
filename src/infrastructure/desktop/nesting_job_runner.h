#ifndef AIPACKAGING_INFRASTRUCTURE_NESTING_JOB_RUNNER_H
#define AIPACKAGING_INFRASTRUCTURE_NESTING_JOB_RUNNER_H

#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <polygonworkspaceview.h>

/// Выполняет внутреннюю реализацию в одном рабочем потоке и доставляет события через диспетчер.
class StdThreadNestingJobRunner final : public INestingJobRunner
{
public:
  /// Сохраняет внутреннюю реализацию, диспетчер и владельца публикуемых решений.
  StdThreadNestingJobRunner(std::shared_ptr<IPolygonNestingBackend> backend, std::shared_ptr<IApplicationDispatcher> dispatcher,
                            std::shared_ptr<IPolygonDocumentGateway> documents);
  /// Запрашивает остановку и дожидается завершения рабочего потока.
  ~StdThreadNestingJobRunner() override;
  /// Запускает работу, если другая работа не выполняется.
  std::optional<NestingJobHandle> start(PolygonDocumentHandle document, const NestingRunRequest & request,
                                        NestingJobCallbacks callbacks, std::string & error) override;
  /// Запрашивает остановку совпадающей активной работы.
  void cancel(NestingJobHandle job) noexcept override;

private:
  std::shared_ptr<IPolygonNestingBackend> backend_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::shared_ptr<IPolygonDocumentGateway> documents_;
  std::mutex mutex_;
  std::jthread worker_;
  std::uint64_t nextJob_ = 1;
  std::optional<NestingJobHandle> activeJob_;
};

#endif
