#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_DRAFT_JOBS_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_DRAFT_JOBS_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <polygondraftcontracts.h>
#include <polygonworkspaceview.h>

/// Последовательно выполняет атомарные записи и удерживает только последнее ожидающее поколение.
class StdThreadPolygonDraftJobRunner final : public IPolygonDraftJobRunner
{
public:
  /// Запускает единственный рабочий поток поверх шлюза документов и прикладного диспетчера.
  StdThreadPolygonDraftJobRunner(std::shared_ptr<IPolygonEditableDocumentGateway> gateway,
                                 std::shared_ptr<IApplicationDispatcher> dispatcher);
  /// Завершает последнюю ожидающую запись и присоединяет рабочий поток.
  ~StdThreadPolygonDraftJobRunner() override;
  /// Атомарно заменяет ещё не начатый запрос более новым поколением.
  std::optional<PolygonDraftJobHandle> submit(PolygonDraftSaveRequest request, PolygonDraftJobCallback callback,
                                              std::string & error) override;
  /// Ожидает опустошения очереди, не выбрасывая исключений.
  void flush() noexcept override;

private:
  /// Хранит один запрос вместе с идентификатором и итоговым обработчиком.
  struct PendingJob
  {
    PolygonDraftJobHandle handle;
    PolygonDraftSaveRequest request;
    PolygonDraftJobCallback callback;
  };

  /// Последовательно извлекает последнее поколение, записывает его и публикует итог.
  void run(const std::stop_token & stopToken) noexcept;
  /// Выполняет основной цикл с обычной семантикой исключений под внешней невыбрасывающей границей.
  void runLoop(const std::stop_token & stopToken);
  /// Сбрасывает состояние очереди после непредвиденного отказа и будит ожидающие потоки.
  void abandonJobs() noexcept;

  std::shared_ptr<IPolygonEditableDocumentGateway> gateway_;
  std::shared_ptr<IApplicationDispatcher> dispatcher_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::shared_ptr<PendingJob> pending_;
  bool active_ = false;
  std::uint64_t nextJob_ = 1;
  std::jthread worker_;
};

#endif
