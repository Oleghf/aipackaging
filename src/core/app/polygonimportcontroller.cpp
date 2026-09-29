#include <utility>

#include <polygondocumentcontroller.h>
#include <polygonimportcontroller.h>

/// Сохраняет независимые порты и не начинает работу до явного действия пользователя.
PolygonImportController::PolygonImportController(std::shared_ptr<IPolygonImportOutput> output,
                                                 std::shared_ptr<IPolygonImportJobRunner> jobs,
                                                 std::shared_ptr<PolygonDocumentController> documents)
  : output_(std::move(output))
  , jobs_(std::move(jobs))
  , documents_(std::move(documents))
{
  publish();
}

/// Запрашивает остановку потока и освобождает результат, не переданный документному контроллеру.
PolygonImportController::~PolygonImportController()
{
  if (activeJob_ && jobs_)
    jobs_->cancel(*activeJob_);
  releaseOwned();
}

/// Формирует обработчики через слабую ссылку, чтобы окно не продлевало время жизни контроллера.
PolygonImportActions PolygonImportController::actions()
{
  const std::weak_ptr<PolygonImportController> weak = weak_from_this();
  PolygonImportActions result;
  result.inspect = [weak](const PolygonImportInspectionRequest & request)
  {
    if (const auto self = weak.lock())
      self->inspect(request);
  };
  result.build = [weak](const PolygonImportConfiguration & configuration)
  {
    if (const auto self = weak.lock())
      self->build(configuration);
  };
  result.cancel = [weak]()
  {
    if (const auto self = weak.lock())
      self->cancel();
  };
  result.accept = [weak](bool asDraft)
  {
    if (const auto self = weak.lock())
      self->accept(asDraft);
  };
  result.close = [weak]()
  {
    if (const auto self = weak.lock())
      self->close();
  };
  return result;
}

/// Возвращает копию согласованного состояния без доступа к внутренним идентификаторам владения.
PolygonImportSnapshot PolygonImportController::snapshot() const
{
  return snapshot_;
}

/// Освобождает прежний сеанс и запускает одну фоновую проверку нового пути.
void PolygonImportController::inspect(const PolygonImportInspectionRequest & request)
{
  if (!jobs_ || request.filePath.empty() || activeJob_)
    return;
  releaseOwned();
  snapshot_ = {};
  snapshot_.state = PolygonImportState::Inspecting;
  snapshot_.statusText = "Чтение и анализ DXF…";
  publish();
  const std::weak_ptr<PolygonImportController> weak = weak_from_this();
  PolygonImportJobCallbacks callbacks;
  callbacks.inspected = [weak](PolygonImportJobHandle job, PolygonImportInspection inspection)
  {
    if (const auto self = weak.lock())
      return self->acceptInspection(job, std::move(inspection));
    return false;
  };
  callbacks.failed = [weak](PolygonImportJobHandle job, const std::string & error)
  {
    if (const auto self = weak.lock())
      self->acceptFailure(job, error);
  };
  callbacks.cancelled = [weak](PolygonImportJobHandle job)
  {
    if (const auto self = weak.lock())
      self->acceptCancellation(job);
  };
  std::string error;
  activeJob_ = jobs_->inspect(request, std::move(callbacks), error);
  if (!activeJob_)
  {
    snapshot_.state = PolygonImportState::Error;
    snapshot_.statusText = error;
    publish();
  }
}

/// Запускает построение только для принятого сеанса анализа.
void PolygonImportController::build(const PolygonImportConfiguration & configuration)
{
  if (!jobs_ || !session_ || activeJob_)
    return;
  if (result_)
  {
    jobs_->release(*result_);
    result_.reset();
  }
  snapshot_.state = PolygonImportState::Building;
  snapshot_.statusText = "Построение и точная проверка документа…";
  snapshot_.diagnostics.clear();
  snapshot_.documentDiagnostics.clear();
  publish();
  const std::weak_ptr<PolygonImportController> weak = weak_from_this();
  PolygonImportJobCallbacks callbacks;
  callbacks.completed = [weak](PolygonImportJobHandle job, PolygonImportBuildResult result)
  {
    if (const auto self = weak.lock())
      return self->acceptBuild(job, std::move(result));
    return false;
  };
  callbacks.failed = [weak](PolygonImportJobHandle job, const std::string & error)
  {
    if (const auto self = weak.lock())
      self->acceptFailure(job, error);
  };
  callbacks.cancelled = [weak](PolygonImportJobHandle job)
  {
    if (const auto self = weak.lock())
      self->acceptCancellation(job);
  };
  std::string error;
  activeJob_ = jobs_->build(*session_, configuration, std::move(callbacks), error);
  if (!activeJob_)
  {
    snapshot_.state = PolygonImportState::Error;
    snapshot_.statusText = error;
    publish();
  }
}

/// Запрашивает остановку только актуальной работы без синхронного ожидания.
void PolygonImportController::cancel()
{
  if (activeJob_ && jobs_)
    jobs_->cancel(*activeJob_);
}

/// Передаёт владение построенным документом и закрывает временный сеанс.
void PolygonImportController::accept(bool asDraft)
{
  if (!result_ || !documents_ || (!asDraft && !snapshot_.resultValid))
    return;
  PolygonEditableDocumentLoadResult loaded = std::move(result_->document);
  result_->document.compiled.reset();
  result_.reset();
  if (session_ && jobs_)
    jobs_->release(*session_);
  session_.reset();
  documents_->adoptImported(std::move(loaded));
  snapshot_ = {};
  snapshot_.statusText = asDraft ? "Импортированный черновик принят" : "Импортированная задача принята";
  publish();
}

/// Отменяет работу и удаляет все временные данные, не меняя активный документ.
void PolygonImportController::close()
{
  if (activeJob_ && jobs_)
    jobs_->cancel(*activeJob_);
  releaseOwned();
  activeJob_.reset();
  snapshot_ = {};
  publish();
}

/// Принимает владение сеансом только от совпадающей работы и переводит мастер к настройкам.
bool PolygonImportController::acceptInspection(PolygonImportJobHandle job, PolygonImportInspection inspection)
{
  if (activeJob_ != job)
    return false;
  activeJob_.reset();
  if (!inspection.success || !inspection.session)
  {
    snapshot_.state = PolygonImportState::Error;
    snapshot_.statusText = inspection.error.empty() ? "DXF не удалось проанализировать" : inspection.error;
    publish();
    return false;
  }
  session_ = inspection.session;
  snapshot_.inspection = std::move(inspection);
  snapshot_.state = PolygonImportState::Ready;
  snapshot_.statusText = "DXF проанализирован; проверьте параметры импорта";
  publish();
  return true;
}

/// Принимает построенный документ и вычисляет доступные варианты завершения мастера.
bool PolygonImportController::acceptBuild(PolygonImportJobHandle job, PolygonImportBuildResult result)
{
  if (activeJob_ != job)
    return false;
  activeJob_.reset();
  if (!result.success)
  {
    snapshot_.state = PolygonImportState::Error;
    snapshot_.statusText = result.error.empty() ? "Документ не удалось построить" : result.error;
    snapshot_.diagnostics = std::move(result.diagnostics);
    publish();
    return false;
  }
  snapshot_.diagnostics = result.diagnostics;
  snapshot_.documentDiagnostics = result.document.diagnostics;
  snapshot_.resultValid = result.document.compiled.has_value();
  result_ = std::move(result);
  snapshot_.state = PolygonImportState::Completed;
  snapshot_.statusText = snapshot_.resultValid ? "Документ готов к импорту" : "Документ можно сохранить только как черновик";
  publish();
  return true;
}

/// Публикует ошибку только для актуальной работы, не уничтожая сеанс анализа.
void PolygonImportController::acceptFailure(PolygonImportJobHandle job, const std::string & error)
{
  if (activeJob_ != job)
    return;
  activeJob_.reset();
  snapshot_.state = PolygonImportState::Error;
  snapshot_.statusText = error;
  publish();
}

/// Возвращает мастер к готовому сеансу либо в пустое состояние после отмены чтения.
void PolygonImportController::acceptCancellation(PolygonImportJobHandle job)
{
  if (activeJob_ != job)
    return;
  activeJob_.reset();
  snapshot_.state = session_ ? PolygonImportState::Ready : PolygonImportState::Cancelled;
  snapshot_.statusText = "Импорт отменён";
  publish();
}

/// Освобождает снимок точной задачи раньше сеанса, из которого он был построен.
void PolygonImportController::releaseOwned() noexcept
{
  if (!jobs_)
    return;
  if (result_)
    jobs_->release(*result_);
  result_.reset();
  if (session_)
    jobs_->release(*session_);
  session_.reset();
}

/// Согласует доступность команд с состоянием работы и наличием результата.
void PolygonImportController::publish()
{
  snapshot_.canBuild = session_.has_value() && !activeJob_;
  snapshot_.canAcceptProblem = result_.has_value() && snapshot_.resultValid && !activeJob_;
  snapshot_.canAcceptDraft = result_.has_value() && !activeJob_;
  if (output_)
    output_->presentPolygonImport(snapshot_);
}
