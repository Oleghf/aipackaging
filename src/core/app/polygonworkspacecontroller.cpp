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
                                                       std::shared_ptr<INestingJobRunner> jobs,
                                                       std::shared_ptr<IPolygonModelGateway> models)
  : output_(std::move(output))
  , documents_(std::move(documents))
  , jobs_(std::move(jobs))
  , models_(std::move(models))
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
  releaseModel();
  if (document_.state().handle)
    documents_->release(*document_.state().handle);
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
  result.openModel = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      return self->openModel(path);
    return false;
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

/// Загружает комплект через порт и заменяет текущую модель только после полной проверки.
bool PolygonWorkspaceController::openModel(const std::string & directory)
{
  if (directory.empty() || snapshot_.state == PolygonWorkspaceState::Running || !models_)
    return false;
  PolygonModelLoadResult loaded = models_->load(directory);
  if (!loaded.success)
  {
    snapshot_.modelStatusText = "Ошибка загрузки модели: " + loaded.error;
    publish();
    return false;
  }
  releaseModel();
  model_ = loaded.model;
  snapshot_.modelReady = true;
  snapshot_.modelId = std::move(loaded.modelId);
  snapshot_.modelSha256 = std::move(loaded.modelSha256);
  snapshot_.modelStatusText = "Модель проверена";
  publish();
  return true;
}

/// Загружает документ через порт и заменяет текущий только после полного успеха.
void PolygonWorkspaceController::openProblem(const std::string & filePath)
{
  if (filePath.empty() || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  PolygonDocumentLoadResult loaded = documents_->load(filePath);
  if (!loaded.success)
  {
    if (!document_.state().handle)
      snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Ошибка загрузки: " + loaded.error;
    publish();
    return;
  }

  releaseSolution();
  if (document_.state().handle)
    documents_->release(*document_.state().handle);
  document_.replace(loaded.document, PolygonDocumentSource::ProblemFile, filePath, true);
  const std::string modelId = snapshot_.modelId;
  const std::string modelSha256 = snapshot_.modelSha256;
  const std::string modelStatus = snapshot_.modelStatusText;
  snapshot_ = {};
  snapshot_.modelReady = model_.has_value();
  snapshot_.modelId = modelId;
  snapshot_.modelSha256 = modelSha256;
  snapshot_.modelStatusText = modelStatus;
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
  if (!document_.state().handle || activeJob_)
    return;
  if (!validRequest(request))
  {
    snapshot_.statusText = "Параметры решателя должны быть положительными";
    publish();
    return;
  }
  if (request.method != NestingMethod::Baseline && !model_)
  {
    snapshot_.statusText = "Выбранный способ недоступен: требуется проверенная модель";
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
  NestingRunRequest effectiveRequest = request;
  effectiveRequest.model = request.method == NestingMethod::Baseline ? std::nullopt : model_;
  const std::optional<NestingJobHandle> job =
    jobs_->start(*document_.state().handle, effectiveRequest, std::move(callbacks), error);
  if (!job)
  {
    snapshot_.statusText = "Не удалось запустить поиск: " + error;
    publish();
    return;
  }
  activeJob_ = job;
  document_.beginRun();
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
  snapshot_.canRun = document_.state().handle.has_value() && document_.state().valid && !running;
  snapshot_.canCancel = running;
  snapshot_.canSave = solution_.has_value() && !running;
  snapshot_.canLoadModel = !running && static_cast<bool>(models_);
  snapshot_.modelReady = model_.has_value();
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
  if (!result.diagnostic.empty())
    snapshot_.modelStatusText = std::move(result.diagnostic);

  if (result.completion == NestingCompletion::Cancelled)
  {
    snapshot_.state = PolygonWorkspaceState::Cancelled;
    snapshot_.statusText = "Поиск отменён; показано последнее частичное решение";
    if (result.solution)
      documents_->release(*result.solution);
    document_.finishRun(false);
  }
  else
  {
    snapshot_.state = PolygonWorkspaceState::Completed;
    solution_ = result.solution;
    document_.finishRun(solution_.has_value());
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
  document_.finishRun(solution_.has_value());
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
  document_.clearSolution();
}

/// Передаёт освобождение модели её шлюзу и очищает опубликованную идентичность.
void PolygonWorkspaceController::releaseModel() noexcept
{
  if (model_ && models_)
    models_->release(*model_);
  model_.reset();
  snapshot_.modelReady = false;
  snapshot_.modelId.clear();
  snapshot_.modelSha256.clear();
  snapshot_.modelStatusText.clear();
}
