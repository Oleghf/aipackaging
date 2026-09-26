#include <utility>

#include <polygondocumentcontroller.h>
#include <polygonworkspacecontroller.h>

/// Сохраняет независимые порты и общий владелец признаков активного документа.
PolygonDocumentController::PolygonDocumentController(std::shared_ptr<IPolygonEditableDocumentGateway> gateway,
                                                     std::shared_ptr<PolygonWorkspaceController> workspace,
                                                     std::shared_ptr<ActivePolygonDocument> activeDocument,
                                                     std::string autosavePath)
  : gateway_(std::move(gateway))
  , workspace_(std::move(workspace))
  , activeDocument_(std::move(activeDocument))
  , autosavePath_(std::move(autosavePath))
{
}

/// Не удаляет восстановительные данные: решение о них принимает только пользовательский сценарий.
PolygonDocumentController::~PolygonDocumentController() = default;

/// Формирует слабые обработчики, не продлевающие время жизни контроллера после закрытия окна.
void PolygonDocumentController::bindActions(PolygonWorkspaceActions & actions)
{
  const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
  actions.openProblem = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      self->openDocument(path);
  };
  actions.saveDocument = [weak](const std::string & path, bool asDraft)
  {
    if (const auto self = weak.lock())
      self->saveDocument(path, asDraft);
  };
  actions.autosaveDocument = [weak]()
  {
    if (const auto self = weak.lock())
      self->autosave();
  };
  actions.restoreRecovery = [weak]()
  {
    if (const auto self = weak.lock())
      self->restoreRecovery();
  };
  actions.deleteRecovery = [weak]()
  {
    if (const auto self = weak.lock())
      self->deleteRecovery();
  };
}

/// Читает только служебные сведения автоматического файла и публикует их рабочей области.
void PolygonDocumentController::inspectRecovery()
{
  workspace_->presentRecovery(gateway_->inspectRecovery(autosavePath_));
}

/// Откладывает файловую загрузку до итогового события активного поиска.
void PolygonDocumentController::openDocument(const std::string & filePath)
{
  if (filePath.empty())
    return;
  const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
  workspace_->requestDocumentReplacement(
    [weak, filePath]()
    {
      if (const auto self = weak.lock())
        self->loadAfterStop(filePath, false);
    });
}

/// Проверяет условия формата и меняет чистую точку только после успешной записи.
void PolygonDocumentController::saveDocument(const std::string & filePath, bool asDraft)
{
  if (filePath.empty() || !document_ || activeDocument_->state().running)
    return;
  PolygonDocumentOperationResult result;
  if (asDraft)
  {
    result = gateway_->saveDraft(filePath, *document_, PolygonDocumentSource::Draft, filePath, generation_, baseFingerprint_);
    if (result.success)
      activeDocument_->markSaved(PolygonDocumentSource::Draft, filePath);
  }
  else
  {
    if (!activeDocument_->state().valid)
    {
      workspace_->reportDocumentOperation("Некорректный документ можно сохранить только как черновик");
      return;
    }
    result = gateway_->saveProblem(filePath, *document_);
    if (result.success)
      activeDocument_->markSaved(PolygonDocumentSource::ProblemFile, filePath);
  }
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка сохранения документа: " + result.error);
    return;
  }
  baseFingerprint_ = gateway_->sourceFingerprint(filePath);
  if (!clearRecoveryAfterSave(filePath))
    return;
  workspace_->reportDocumentOperation(asDraft ? "Черновик сохранён" : "Задача сохранена");
  workspace_->refreshDocumentState();
}

/// Записывает новое поколение только для изменённого документа и не очищает его признак изменения.
void PolygonDocumentController::autosave()
{
  if (!document_ || !activeDocument_->state().dirty || autosavePath_.empty())
    return;
  const std::uint64_t nextGeneration = generation_ + 1;
  const auto & state = activeDocument_->state();
  const PolygonDocumentOperationResult result =
    gateway_->saveDraft(autosavePath_, *document_, state.source, state.sourceIdentifier, nextGeneration, baseFingerprint_);
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка автосохранения: " + result.error);
    return;
  }
  generation_ = nextGeneration;
  inspectRecovery();
}

/// Отменяет поиск при необходимости и загружает восстановительный файл как отдельный грязный документ.
void PolygonDocumentController::restoreRecovery()
{
  if (autosavePath_.empty())
    return;
  const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
  workspace_->requestDocumentReplacement(
    [weak]()
    {
      if (const auto self = weak.lock())
        self->loadAfterStop(self->autosavePath_, true);
    });
}

/// Удаляет файл только через шлюз и сохраняет карточку с ошибкой при отказе.
void PolygonDocumentController::deleteRecovery()
{
  const PolygonDocumentOperationResult result = gateway_->removeRecovery(autosavePath_);
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка удаления черновика: " + result.error);
    return;
  }
  inspectRecovery();
}

/// Выбирает обычную или восстановительную загрузку и сохраняет прежний документ при отказе.
void PolygonDocumentController::loadAfterStop(const std::string & filePath, bool recovery)
{
  PolygonEditableDocumentLoadResult loaded = recovery ? gateway_->loadRecovery(filePath) : gateway_->load(filePath);
  if (!loaded.success)
  {
    workspace_->reportDocumentOperation("Ошибка загрузки: " + loaded.error);
    if (recovery)
      inspectRecovery();
    return;
  }
  acceptLoaded(std::move(loaded), recovery);
}

/// Сохраняет модель у владельца и передаёт рабочей области только согласованный снимок.
void PolygonDocumentController::acceptLoaded(PolygonEditableDocumentLoadResult loaded, bool recovered)
{
  document_ = loaded.document;
  baseFingerprint_ = loaded.baseFingerprint;
  generation_ = loaded.generation;
  if (recovered)
  {
    loaded.source = PolygonDocumentSource::RecoveredDraft;
  }
  const bool sourceChanged = loaded.sourceChanged;
  workspace_->acceptEditableDocument(std::move(loaded), recovered);
  if (recovered && sourceChanged)
    workspace_->reportDocumentOperation(
      "Черновик восстановлен; исходный файл изменился, поэтому требуется новый путь сохранения");
}

/// Удаляет восстановительный файл после записи пользователем и обновляет карточку.
bool PolygonDocumentController::clearRecoveryAfterSave(const std::string & savedPath)
{
  if (savedPath == autosavePath_)
  {
    inspectRecovery();
    return true;
  }
  const PolygonDocumentOperationResult removed = gateway_->removeRecovery(autosavePath_);
  if (!removed.success)
  {
    workspace_->reportDocumentOperation("Документ сохранён, но автоматический черновик удалить не удалось: " + removed.error);
    return false;
  }
  inspectRecovery();
  return true;
}
