#include <utility>

#include <polygonworkspacecontroller.h>

namespace
{
/// Проверяет положительные бюджеты, обязательные для выбранных алгоритмов.
bool validRequest(const NestingRunRequest & request)
{
  return request.randomIterations > 0 && request.beamWidth > 0 && request.maxExpandedStates > 0 && request.neuralRollouts > 0 &&
         request.fallbackRandomIterations > 0;
}
} // namespace

/// Сохраняет прикладные порты и публикует исходное пустое состояние.
PolygonWorkspaceController::PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceOutput> output,
                                                       std::shared_ptr<IPolygonDocumentGateway> documents,
                                                       std::shared_ptr<INestingJobRunner> jobs)
  : output_(std::move(output))
  , documents_(std::move(documents))
  , jobs_(std::move(jobs))
{
  snapshot_.statusText = "Откройте задачу `polygon_problem` v1";
  publish();
}

/// Запрашивает отмену и освобождает зарегистрированные артефакты без ожидания потока.
PolygonWorkspaceController::~PolygonWorkspaceController()
{
  if (activeJob_)
    jobs_->cancel(*activeJob_);
  releaseSolution();
  if (document_)
    documents_->release(*document_);
}

/// Создаёт функции, которые удерживают контроллер только на время конкретного вызова.
PolygonWorkspaceActions PolygonWorkspaceController::actions()
{
  const std::weak_ptr<PolygonWorkspaceController> weak = weak_from_this();
  PolygonWorkspaceActions result;
  result.openProblem = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      self->openProblem(path);
  };
  result.saveSolution = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      self->saveSolution(path);
  };
  result.start = [weak](const NestingRunRequest & request)
  {
    if (const auto self = weak.lock())
      self->start(request);
  };
  result.cancel = [weak]()
  {
    if (const auto self = weak.lock())
      self->cancel();
  };
  return result;
}

/// Загружает документ через порт и заменяет текущий только после полного успеха.
void PolygonWorkspaceController::openProblem(const std::string & filePath)
{
  if (filePath.empty() || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  PolygonDocumentLoadResult loaded = documents_->load(filePath);
  if (!loaded.success)
  {
    if (!document_)
      snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Ошибка загрузки: " + loaded.error;
    publish();
    return;
  }

  releaseSolution();
  if (document_)
    documents_->release(*document_);
  document_ = loaded.document;
  snapshot_ = {};
  snapshot_.state = PolygonWorkspaceState::Ready;
  snapshot_.problemId = std::move(loaded.problemId);
  snapshot_.statusText = "Задача загружена";
  snapshot_.scene = std::move(loaded.scene);
  snapshot_.unplacedInstances = std::move(loaded.unplacedInstances);
  publish();
}

/// Делегирует запись шлюзу только для зарегистрированного проверенного результата.
void PolygonWorkspaceController::saveSolution(const std::string & filePath)
{
  if (filePath.empty() || !solution_ || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  const PolygonDocumentOperationResult saved = documents_->save(filePath, *solution_);
  snapshot_.statusText = saved.success ? "Решение сохранено" : "Ошибка сохранения: " + saved.error;
  publish();
}

/// Проверяет запрос, создаёт функции событий и передаёт выполнение средству запуска.
void PolygonWorkspaceController::start(const NestingRunRequest & request)
{
  if (!document_ || activeJob_)
    return;
  if (!validRequest(request))
  {
    snapshot_.statusText = "Параметры решателя должны быть положительными";
    publish();
    return;
  }
  if (request.method != NestingMethod::Baseline)
  {
    snapshot_.statusText = "Выбранный способ раскроя пока недоступен";
    publish();
    return;
  }

  const std::weak_ptr<PolygonWorkspaceController> weak = weak_from_this();
  NestingJobCallbacks callbacks;
  callbacks.progress = [weak](NestingJobHandle job, const NestingProgress & progress)
  {
    if (const auto self = weak.lock())
      self->acceptProgress(job, progress);
  };
  callbacks.completed = [weak](NestingJobHandle job, NestingRunResult result)
  {
    if (const auto self = weak.lock())
      self->acceptResult(job, std::move(result));
  };
  callbacks.failed = [weak](NestingJobHandle job, const std::string & error)
  {
    if (const auto self = weak.lock())
      self->acceptFailure(job, error);
  };

  std::string error;
  const std::optional<NestingJobHandle> job = jobs_->start(*document_, request, std::move(callbacks), error);
  if (!job)
  {
    snapshot_.statusText = "Не удалось запустить поиск: " + error;
    publish();
    return;
  }
  activeJob_ = job;
  snapshot_.state = PolygonWorkspaceState::Running;
  snapshot_.statusText = "Выполняется полигональный поиск";
  snapshot_.progress = {};
  publish();
}

/// Передаёт запрос отмены активному средству запуска и немедленно обновляет сообщение.
void PolygonWorkspaceController::cancel()
{
  if (!activeJob_)
    return;
  jobs_->cancel(*activeJob_);
  snapshot_.statusText = "Запрошена отмена…";
  publish();
}

/// Возвращает копию последнего прикладного снимка.
PolygonWorkspaceSnapshot PolygonWorkspaceController::snapshot() const
{
  return snapshot_;
}

/// Вычисляет доступность действий из состояния и передаёт снимок выходному порту.
void PolygonWorkspaceController::publish()
{
  const bool running = snapshot_.state == PolygonWorkspaceState::Running;
  snapshot_.canOpen = !running;
  snapshot_.canRun = document_.has_value() && !running;
  snapshot_.canCancel = running;
  snapshot_.canSave = solution_.has_value() && !running;
  if (output_)
    output_->presentPolygonWorkspace(snapshot_);
}

/// Игнорирует устаревшие сообщения и обновляет ход только текущей работы.
void PolygonWorkspaceController::acceptProgress(NestingJobHandle job, const NestingProgress & progress)
{
  if (!activeJob_ || job != *activeJob_ || snapshot_.state != PolygonWorkspaceState::Running)
    return;
  snapshot_.progress = progress;
  publish();
}

/// Публикует подготовленную сцену и сохраняет только выданный инфраструктурой артефакт.
void PolygonWorkspaceController::acceptResult(NestingJobHandle job, NestingRunResult result)
{
  if (!activeJob_ || job != *activeJob_ || snapshot_.state != PolygonWorkspaceState::Running)
  {
    if (result.solution)
      documents_->release(*result.solution);
    return;
  }

  activeJob_.reset();
  releaseSolution();
  snapshot_.solverName = std::move(result.implementationName);
  snapshot_.solutionStatus = std::move(result.solutionStatus);
  snapshot_.partial = result.partial;
  snapshot_.objective = result.objective;
  snapshot_.metrics = result.metrics;
  snapshot_.scene = std::move(result.scene);
  snapshot_.unplacedInstances = std::move(result.unplacedInstances);

  if (result.completion == NestingCompletion::Cancelled)
  {
    snapshot_.state = PolygonWorkspaceState::Cancelled;
    snapshot_.statusText = "Поиск отменён; показано последнее частичное решение";
    if (result.solution)
      documents_->release(*result.solution);
  }
  else
  {
    snapshot_.state = PolygonWorkspaceState::Completed;
    solution_ = result.solution;
    switch (result.completion)
    {
      case NestingCompletion::TimedOut:
        snapshot_.statusText = "Истекло время поиска; показано лучшее проверенное частичное решение";
        break;
      case NestingCompletion::BudgetExhausted:
        snapshot_.statusText = "Исчерпан бюджет; показано лучшее проверенное решение";
        break;
      case NestingCompletion::UnsupportedEnvironment:
        snapshot_.statusText = "Превышен предел среды; показано лучшее проверенное частичное решение";
        break;
      case NestingCompletion::NoSolutionFound:
        snapshot_.statusText = "Полное решение не найдено; показано проверенное частичное решение";
        break;
      default:
        snapshot_.statusText = result.partial ? "Показано лучшее частичное решение" : "Полная раскладка построена";
        break;
    }
  }
  publish();
}

/// Сохраняет предыдущую сцену и переводит только актуальную работу в состояние ошибки.
void PolygonWorkspaceController::acceptFailure(NestingJobHandle job, const std::string & error)
{
  if (!activeJob_ || job != *activeJob_ || snapshot_.state != PolygonWorkspaceState::Running)
    return;
  activeJob_.reset();
  snapshot_.state = PolygonWorkspaceState::Error;
  snapshot_.statusText = "Ошибка внутренней реализации решателя: " + error;
  publish();
}

/// Освобождает зарегистрированное решение и очищает прикладной идентификатор.
void PolygonWorkspaceController::releaseSolution() noexcept
{
  if (solution_)
    documents_->release(*solution_);
  solution_.reset();
}
