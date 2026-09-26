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
                                                       std::shared_ptr<IPolygonModelJobRunner> modelJobs,
                                                       std::shared_ptr<ActivePolygonDocument> activeDocument)
  : output_(std::move(output))
  , documents_(std::move(documents))
  , jobs_(std::move(jobs))
  , modelJobs_(std::move(modelJobs))
  , document_(activeDocument ? std::move(activeDocument) : std::make_shared<ActivePolygonDocument>())
{
  snapshot_.statusText = "Откройте задачу `polygon_problem` v1";
  publish();
}

/// Запрашивает отмену и освобождает зарегистрированные артефакты без ожидания потока.
PolygonWorkspaceController::~PolygonWorkspaceController()
{
  if (activeJob_)
    jobs_->cancel(*activeJob_);
  if (activeModelJob_)
    modelJobs_->cancel(*activeModelJob_);
  releaseSolution();
  releaseModel();
  if (document_->state().handle)
    documents_->release(*document_->state().handle);
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
      self->openModel(path);
  };
  result.cancelModelLoad = [weak]()
  {
    if (const auto self = weak.lock())
      self->cancelModelLoad();
  };
  result.forgetModel = [weak]()
  {
    if (const auto self = weak.lock())
      self->forgetModel();
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

/// Создаёт функции событий и передаёт проверку модели фоновому средству запуска.
void PolygonWorkspaceController::openModel(const std::string & directory)
{
  if (directory.empty() || snapshot_.state == PolygonWorkspaceState::Running || !modelJobs_ || activeModelJob_)
    return;
  const std::weak_ptr<PolygonWorkspaceController> weak = weak_from_this();
  PolygonModelJobCallbacks callbacks;
  callbacks.completed = [weak](PolygonModelJobHandle job, PolygonModelLoadResult result)
  {
    if (const auto self = weak.lock())
    {
      self->acceptModel(job, std::move(result));
      return true;
    }
    return false;
  };
  callbacks.failed = [weak](PolygonModelJobHandle job, const std::string & error)
  {
    if (const auto self = weak.lock())
      self->acceptModelFailure(job, error);
  };
  callbacks.cancelled = [weak](PolygonModelJobHandle job)
  {
    if (const auto self = weak.lock())
      self->acceptModelCancellation(job);
  };
  std::string error;
  const auto job = modelJobs_->start(directory, std::move(callbacks), error);
  if (!job)
  {
    snapshot_.modelState = PolygonModelState::Error;
    snapshot_.modelStatusText = "Не удалось начать проверку модели: " + error;
    publish();
    return;
  }
  activeModelJob_ = job;
  snapshot_.modelState = PolygonModelState::Loading;
  snapshot_.modelStatusText = "Проверяется комплект модели…";
  publish();
}

/// Передаёт запрос остановки актуальной фоновой проверке модели.
void PolygonWorkspaceController::cancelModelLoad()
{
  if (activeModelJob_ && modelJobs_)
    modelJobs_->cancel(*activeModelJob_);
}

/// Освобождает проверенную модель только вне активной работы.
void PolygonWorkspaceController::forgetModel()
{
  if (snapshot_.state == PolygonWorkspaceState::Running || activeModelJob_)
    return;
  releaseModel();
  snapshot_.modelState = PolygonModelState::NotSelected;
  snapshot_.modelStatusText = "Модель не выбрана";
  publish();
}

/// Загружает документ через порт и заменяет текущий только после полного успеха.
void PolygonWorkspaceController::openProblem(const std::string & filePath)
{
  if (filePath.empty() || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  PolygonDocumentLoadResult loaded = documents_->load(filePath);
  if (!loaded.success)
  {
    if (!document_->state().handle)
      snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Ошибка загрузки: " + loaded.error;
    publish();
    return;
  }

  releaseSolution();
  if (document_->state().handle)
    documents_->release(*document_->state().handle);
  document_->replace(loaded.document, PolygonDocumentSource::ProblemFile, filePath, true);
  const std::string modelId = snapshot_.modelId;
  const std::string modelSha256 = snapshot_.modelSha256;
  const std::string modelStatus = snapshot_.modelStatusText;
  const PolygonModelState modelState = snapshot_.modelState;
  const PolygonRecoveryCandidate recovery = snapshot_.recovery;
  snapshot_ = {};
  snapshot_.modelReady = model_.has_value();
  snapshot_.modelId = modelId;
  snapshot_.modelSha256 = modelSha256;
  snapshot_.modelStatusText = modelStatus;
  snapshot_.modelState = modelState;
  snapshot_.state = PolygonWorkspaceState::Ready;
  snapshot_.problemId = std::move(loaded.problemId);
  snapshot_.document = std::move(loaded.summary);
  snapshot_.document.sourceIdentifier = filePath;
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
  if (!document_->state().handle || activeJob_)
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
    {
      self->acceptResult(job, std::move(result));
      return true;
    }
    return false;
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
    jobs_->start(*document_->state().handle, effectiveRequest, std::move(callbacks), error);
  if (!job)
  {
    snapshot_.statusText = "Не удалось запустить поиск: " + error;
    publish();
    return;
  }
  activeJob_ = job;
  document_->beginRun();
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

/// Сохраняет только последнюю замену и при необходимости сначала запрашивает отмену текущей работы.
void PolygonWorkspaceController::requestDocumentReplacement(std::function<void()> replacement)
{
  if (!replacement)
    return;
  if (!activeJob_)
  {
    replacement();
    return;
  }
  pendingDocumentReplacement_ = std::move(replacement);
  jobs_->cancel(*activeJob_);
  snapshot_.statusText = "Текущий поиск отменяется перед заменой документа…";
  publish();
}

/// Заменяет задачу и сцену одним согласованным обновлением после успешной загрузки редактируемой модели.
void PolygonWorkspaceController::acceptEditableDocument(PolygonEditableDocumentLoadResult loaded, bool dirty)
{
  if (activeJob_ || !loaded.success)
    return;
  const std::optional<PolygonDocumentHandle> nextHandle =
    loaded.compiled ? std::optional<PolygonDocumentHandle>{loaded.compiled->document} : std::nullopt;
  const bool valid = loaded.compiled.has_value();
  const std::optional<PolygonDocumentHandle> previousHandle = document_->state().handle;
  if (!document_->replaceEditable(nextHandle, loaded.source, loaded.sourceIdentifier, valid))
  {
    if (nextHandle)
      documents_->release(*nextHandle);
    return;
  }
  if (dirty)
    document_->markChanged(valid);

  releaseSolution();
  if (previousHandle)
    documents_->release(*previousHandle);
  const std::string modelId = snapshot_.modelId;
  const std::string modelSha256 = snapshot_.modelSha256;
  const std::string modelStatus = snapshot_.modelStatusText;
  const PolygonModelState modelState = snapshot_.modelState;
  const PolygonRecoveryCandidate recovery = snapshot_.recovery;
  snapshot_ = {};
  snapshot_.modelReady = model_.has_value();
  snapshot_.modelId = modelId;
  snapshot_.modelSha256 = modelSha256;
  snapshot_.modelStatusText = modelStatus;
  snapshot_.modelState = modelState;
  snapshot_.recovery = recovery;
  snapshot_.state = PolygonWorkspaceState::Ready;
  snapshot_.problemId = std::move(loaded.problemId);
  snapshot_.document = std::move(loaded.summary);
  snapshot_.document.sourceIdentifier = loaded.sourceIdentifier;
  snapshot_.documentDiagnostics = std::move(loaded.diagnostics);
  snapshot_.scene = std::move(loaded.scene);
  snapshot_.unplacedInstances = std::move(loaded.unplacedInstances);
  snapshot_.statusText =
    valid ? (dirty ? "Черновик восстановлен" : "Документ загружен") : "Черновик загружен; исправьте ошибки перед запуском";
  publish();
}

/// Заменяет только сведения карточки восстановления и сохраняет текущее рабочее состояние.
void PolygonWorkspaceController::presentRecovery(PolygonRecoveryCandidate recovery)
{
  snapshot_.recovery = std::move(recovery);
  publish();
}

/// Обновляет пользовательское сообщение, не очищая документ, сцену или решение.
void PolygonWorkspaceController::reportDocumentOperation(std::string message)
{
  snapshot_.statusText = std::move(message);
  publish();
}

/// Повторно вычисляет доступность действий из общего владельца состояния документа.
void PolygonWorkspaceController::refreshDocumentState()
{
  publish();
}

/// Вычисляет доступность действий из состояния и передаёт снимок выходному порту.
void PolygonWorkspaceController::publish()
{
  const bool running = snapshot_.state == PolygonWorkspaceState::Running;
  snapshot_.canOpen = !running;
  snapshot_.canRun = document_->state().handle.has_value() && document_->state().valid && !running && !activeModelJob_;
  snapshot_.canCancel = running;
  snapshot_.canSave = solution_.has_value() && !running;
  snapshot_.canLoadModel = !running && !activeModelJob_ && static_cast<bool>(modelJobs_);
  snapshot_.canCancelModelLoad = activeModelJob_.has_value();
  snapshot_.modelReady = model_.has_value();
  const auto & document = document_->state();
  snapshot_.hasDocument = document.present;
  snapshot_.canSaveDocument = document.present && !running;
  snapshot_.canSaveProblem = document.present && document.valid && !running;
  snapshot_.documentSource = document.source;
  snapshot_.documentDirty = document.dirty;
  snapshot_.documentValid = document.valid;
  snapshot_.solutionStale = document.solutionStale;
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
    document_->finishRun(false);
  }
  else
  {
    snapshot_.state = PolygonWorkspaceState::Completed;
    solution_ = result.solution;
    document_->finishRun(solution_.has_value());
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
  continueDocumentReplacement();
}

/// Сохраняет предыдущую сцену и переводит только актуальную работу в состояние ошибки.
void PolygonWorkspaceController::acceptFailure(NestingJobHandle job, const std::string & error)
{
  if (!activeJob_ || job != *activeJob_ || snapshot_.state != PolygonWorkspaceState::Running)
    return;
  activeJob_.reset();
  document_->finishRun(solution_.has_value());
  snapshot_.state = PolygonWorkspaceState::Error;
  snapshot_.statusText = "Ошибка внутренней реализации решателя: " + error;
  publish();
  continueDocumentReplacement();
}

/// Заменяет модель только после полной успешной проверки актуальной работы.
void PolygonWorkspaceController::acceptModel(PolygonModelJobHandle job, PolygonModelLoadResult result)
{
  if (!activeModelJob_ || job != *activeModelJob_)
  {
    if (result.success && result.model && modelJobs_)
      modelJobs_->release(result.model);
    return;
  }
  activeModelJob_.reset();
  if (!result.success)
  {
    snapshot_.modelState = PolygonModelState::Error;
    snapshot_.modelStatusText = "Ошибка загрузки модели: " + result.error;
    publish();
    return;
  }
  releaseModel();
  model_ = result.model;
  snapshot_.modelReady = true;
  snapshot_.modelState = PolygonModelState::Ready;
  snapshot_.modelId = std::move(result.modelId);
  snapshot_.modelSha256 = std::move(result.modelSha256);
  snapshot_.modelStatusText = "Модель проверена";
  publish();
}

/// Сохраняет прежнюю модель и публикует ошибку актуальной проверки.
void PolygonWorkspaceController::acceptModelFailure(PolygonModelJobHandle job, const std::string & error)
{
  if (!activeModelJob_ || job != *activeModelJob_)
    return;
  activeModelJob_.reset();
  snapshot_.modelState = PolygonModelState::Error;
  snapshot_.modelStatusText = "Ошибка загрузки модели: " + error;
  publish();
}

/// Сохраняет прежнюю модель после согласованной отмены проверки.
void PolygonWorkspaceController::acceptModelCancellation(PolygonModelJobHandle job)
{
  if (!activeModelJob_ || job != *activeModelJob_)
    return;
  activeModelJob_.reset();
  snapshot_.modelState = model_ ? PolygonModelState::Ready : PolygonModelState::NotSelected;
  snapshot_.modelStatusText = model_ ? "Проверка отменена; сохранена прежняя модель" : "Проверка модели отменена";
  publish();
}

/// Освобождает зарегистрированное решение и очищает прикладной идентификатор.
void PolygonWorkspaceController::releaseSolution() noexcept
{
  if (solution_)
    documents_->release(*solution_);
  solution_.reset();
  document_->clearSolution();
}

/// Передаёт освобождение модели фоновому средству и очищает опубликованную идентичность.
void PolygonWorkspaceController::releaseModel() noexcept
{
  if (model_ && modelJobs_)
    modelJobs_->release(*model_);
  model_.reset();
  snapshot_.modelReady = false;
  snapshot_.modelId.clear();
  snapshot_.modelSha256.clear();
  snapshot_.modelStatusText.clear();
}

/// Извлекает отложенное действие перед вызовом, чтобы новый запуск не считался частью старой работы.
void PolygonWorkspaceController::continueDocumentReplacement()
{
  if (activeJob_ || !pendingDocumentReplacement_)
    return;
  auto replacement = std::move(pendingDocumentReplacement_);
  pendingDocumentReplacement_ = {};
  replacement();
}
