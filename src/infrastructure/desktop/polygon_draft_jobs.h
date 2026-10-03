#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_DRAFT_JOBS_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_DRAFT_JOBS_H

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

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
  /// Ставит удаление после активной операции и убирает ожидающие старые записи этого пути.
  std::optional<PolygonDraftJobHandle> invalidate(PolygonDraftInvalidationRequest request, PolygonDraftJobCallback callback,
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
    bool invalidation = false;
  };

  /// Публикует операцию с сохранением барьера между удалением и последующей записью.
  std::optional<PolygonDraftJobHandle> enqueue(PolygonDraftSaveRequest request, PolygonDraftJobCallback callback,
                                               bool invalidation, std::string & error);

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
  std::deque<std::shared_ptr<PendingJob>> pending_;
  bool active_ = false;
  std::uint64_t nextJob_ = 1;
  std::unordered_map<std::string, std::uint64_t> publishedOwners_;
  std::jthread worker_;
};

#endif
