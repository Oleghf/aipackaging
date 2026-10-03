#include <algorithm>
#include <memory>
#include <utility>

#include <polygondocumentcontroller.h>
#include <polygonworkspacecontroller.h>

/// Сохраняет независимые порты и общий владелец признаков активного документа.
PolygonDocumentController::PolygonDocumentController(std::shared_ptr<IPolygonEditableDocumentGateway> gateway,
                                                     std::shared_ptr<PolygonWorkspaceController> workspace,
                                                     std::shared_ptr<ActivePolygonDocument> activeDocument,
                                                     std::string autosavePath, std::shared_ptr<IPolygonDraftJobRunner> draftJobs)
  : gateway_(std::move(gateway))
  , workspace_(std::move(workspace))
  , activeDocument_(std::move(activeDocument))
  , draftJobs_(std::move(draftJobs))
  , autosavePath_(std::move(autosavePath))
{
}

/// Не удаляет восстановительные данные: решение о них принимает только пользовательский сценарий.
PolygonDocumentController::~PolygonDocumentController()
{
  if (draftJobs_)
    draftJobs_->flush();
}

/// Формирует слабые обработчики, не продлевающие время жизни контроллера после закрытия окна.
void PolygonDocumentController::bindActions(PolygonWorkspaceActions & actions)
{
  const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
  actions.createDocument = [weak](const std::string & problemId, double width, double height)
  {
    if (const auto self = weak.lock())
      self->createDocument(problemId, width, height);
  };
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
  actions.editDocument = [weak](const aipackaging::editor::EditorCommandBatch & batch)
  {
    if (const auto self = weak.lock())
      self->editDocument(batch);
  };
  actions.undoDocument = [weak]()
  {
    if (const auto self = weak.lock())
      self->undo();
  };
  actions.redoDocument = [weak]()
  {
    if (const auto self = weak.lock())
      self->redo();
  };
  actions.beginEditGesture = [weak]() -> std::uint64_t
  {
    if (const auto self = weak.lock())
      return self->beginGesture();
    return 0;
  };
  actions.cancelEditGesture = [weak](std::uint64_t id)
  {
    if (const auto self = weak.lock())
      self->cancelGesture(id);
  };
  actions.finishEditGesture = [weak](std::uint64_t id)
  {
    if (const auto self = weak.lock())
      self->finishGesture(id);
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

/// Создаёт модель с обязательными исходными полями и публикует её как грязный черновик без пути.
/// Порядок ширины и высоты закреплён существующим прикладным действием и проверяется именованными полями формы.
void PolygonDocumentController::createDocument(const std::string & problemId,
                                               double sheetWidth, // NOLINT(bugprone-easily-swappable-parameters)
                                               double sheetHeight)
{
  if (activeDocument_->state().running)
    return;
  aipackaging::editor::EditablePolygonDocument document;
  document.problemId = problemId;
  document.sheet.width = sheetWidth;
  document.sheet.height = sheetHeight;
  document.manufacturing.curveTolerance = 0.05;
  PolygonEditableDocumentLoadResult loaded = gateway_->compileImported(document, {}, std::nullopt);
  loaded.source = PolygonDocumentSource::Untitled;
  loaded.sourceIdentifier.clear();
  acceptLoaded(std::move(loaded), false);
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
  const auto deferredPath = std::make_shared<const std::string>(filePath);
  workspace_->requestDocumentReplacement(
    [weak, deferredPath]()
    {
      if (const auto self = weak.lock())
        self->loadAfterStop(*deferredPath, false);
    });
}

/// Проверяет условия формата и меняет чистую точку только после успешной записи.
void PolygonDocumentController::saveDocument(const std::string & filePath, bool asDraft)
{
  if (filePath.empty() || !session_ || activeDocument_->state().running)
    return;
  if (draftJobs_)
    draftJobs_->flush();
  PolygonDocumentOperationResult result;
  if (asDraft)
  {
    result =
      gateway_->saveDraft(filePath, session_->document(), PolygonDocumentSource::Draft, filePath, generation_, baseFingerprint_);
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
    result = gateway_->saveProblem(filePath, session_->document());
    if (result.success)
      activeDocument_->markSaved(PolygonDocumentSource::ProblemFile, filePath);
  }
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка сохранения документа: " + result.error);
    return;
  }
  baseFingerprint_ = gateway_->sourceFingerprint(filePath);
  session_->markSaved();
  workspace_->presentEditorHistory(session_->snapshot(), session_->history());
  if (!clearRecoveryAfterSave(filePath))
    return;
  workspace_->reportDocumentOperation(asDraft ? "Черновик сохранён" : "Задача сохранена");
  workspace_->refreshDocumentState();
}

/// Записывает новое поколение только для изменённого документа и не очищает его признак изменения.
void PolygonDocumentController::autosave()
{
  if (!session_ || !activeDocument_->state().dirty || autosavePath_.empty())
    return;
  const std::uint64_t nextGeneration = std::max(generation_, requestedGeneration_) + 1;
  const auto & state = activeDocument_->state();
  if (draftJobs_)
  {
    PolygonDraftSaveRequest request;
    request.filePath = autosavePath_;
    request.document = session_->snapshot();
    request.source = state.source;
    request.sourceIdentifier = state.sourceIdentifier;
    request.generation = nextGeneration;
    request.baseFingerprint = baseFingerprint_;
    request.documentIdentity = documentIdentity_;
    const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
    std::string error;
    const auto job = draftJobs_->submit(
      std::move(request),
      [weak, identity = documentIdentity_](PolygonDraftJobHandle handle, std::uint64_t generation,
                                           const PolygonDocumentOperationResult & result)
      {
        if (const auto self = weak.lock(); self && self->documentIdentity_ == identity)
          self->finishAutosave(handle, generation, result);
      },
      error);
    if (!job)
    {
      workspace_->reportDocumentOperation("Ошибка автосохранения: " + error);
      return;
    }
    requestedGeneration_ = nextGeneration;
    activeDraftJob_ = *job;
    ownsRecovery_ = true;
    return;
  }
  const PolygonDocumentOperationResult result = gateway_->saveDraft(autosavePath_, session_->document(), state.source,
                                                                    state.sourceIdentifier, nextGeneration, baseFingerprint_);
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка автосохранения: " + result.error);
    return;
  }
  generation_ = nextGeneration;
  requestedGeneration_ = nextGeneration;
  recoveryFingerprint_.reset();
  ownsRecovery_ = true;
  inspectRecovery();
}

/// Ставит удаление после уже принятой записи, не ожидая файловой операции в потоке интерфейса.
void PolygonDocumentController::invalidateOwnedRecovery()
{
  if (!ownsRecovery_)
    return;
  const std::uint64_t generation = std::max(generation_, requestedGeneration_) + 1;
  if (draftJobs_)
  {
    const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
    std::string error;
    const auto job = draftJobs_->invalidate(
      {autosavePath_, generation, documentIdentity_, recoveryFingerprint_},
      [weak, identity = documentIdentity_](PolygonDraftJobHandle handle, std::uint64_t value,
                                           const PolygonDocumentOperationResult & result)
      {
        if (const auto self = weak.lock(); self && self->documentIdentity_ == identity)
        {
          if (!result.success && self->activeDraftJob_ == handle && self->requestedGeneration_ == value)
          {
            self->ownsRecovery_ = true;
            self->activeDraftJob_.reset();
            self->workspace_->reportDocumentOperation("Ошибка удаления автоматического черновика: " + result.error);
            return;
          }
          self->finishAutosave(handle, value, result);
        }
      },
      error);
    if (!job)
    {
      workspace_->reportDocumentOperation("Не удалось удалить устаревший автоматический черновик: " + error);
      return;
    }
    requestedGeneration_ = generation;
    activeDraftJob_ = *job;
  }
  else
  {
    if (recoveryFingerprint_ && gateway_->sourceFingerprint(autosavePath_) != recoveryFingerprint_)
    {
      ownsRecovery_ = false;
      inspectRecovery();
      return;
    }
    const auto result = gateway_->removeRecovery(autosavePath_);
    if (!result.success)
    {
      workspace_->reportDocumentOperation("Не удалось удалить устаревший автоматический черновик: " + result.error);
      return;
    }
    generation_ = requestedGeneration_ = generation;
    inspectRecovery();
  }
  ownsRecovery_ = false;
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
  if (draftJobs_)
    draftJobs_->flush();
  const PolygonDocumentOperationResult result = gateway_->removeRecovery(autosavePath_);
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка удаления черновика: " + result.error);
    return;
  }
  inspectRecovery();
}

/// Игнорирует запоздалые итоги и обновляет карточку только для последнего поколения.
void PolygonDocumentController::finishAutosave(PolygonDraftJobHandle job, std::uint64_t generation,
                                               const PolygonDocumentOperationResult & result)
{
  if (!activeDraftJob_ || *activeDraftJob_ != job || generation != requestedGeneration_)
    return;
  activeDraftJob_.reset();
  if (!result.success)
  {
    workspace_->reportDocumentOperation("Ошибка автосохранения: " + result.error);
    return;
  }
  generation_ = generation;
  inspectRecovery();
}

/// Откладывает публикацию импортированного документа до безопасной замены текущего снимка.
void PolygonDocumentController::adoptImported(PolygonEditableDocumentLoadResult loaded)
{
  auto pending = std::make_shared<PolygonEditableDocumentLoadResult>(std::move(loaded));
  const std::weak_ptr<PolygonDocumentController> weak = weak_from_this();
  workspace_->requestDocumentReplacement(
    [weak, pending]() mutable
    {
      if (const auto self = weak.lock())
        self->acceptLoaded(std::move(*pending), false);
    });
}

/// Выбирает обычную или восстановительную загрузку и сохраняет прежний документ при отказе.
void PolygonDocumentController::loadAfterStop(const std::string & filePath, bool recovery)
{
  if (recovery && draftJobs_)
    draftJobs_->flush();
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
  if (!loaded.success)
  {
    workspace_->reportDocumentOperation("Ошибка загрузки: " + loaded.error);
    return;
  }
  const bool dirty =
    recovered || loaded.source == PolygonDocumentSource::Imported || loaded.source == PolygonDocumentSource::Untitled;
  session_.emplace(std::move(loaded.document), !dirty);
  ++documentIdentity_;
  ownsRecovery_ = recovered;
  recoveryFingerprint_ = recovered ? gateway_->sourceFingerprint(autosavePath_) : std::nullopt;
  activeDraftJob_.reset();
  baseFingerprint_ = loaded.baseFingerprint;
  generation_ = std::max({generation_, requestedGeneration_, loaded.generation});
  requestedGeneration_ = generation_;
  if (recovered)
  {
    loaded.source = PolygonDocumentSource::RecoveredDraft;
  }
  const bool sourceChanged = loaded.sourceChanged;
  workspace_->acceptEditableDocument(std::move(loaded), dirty, session_->snapshot(), session_->history());
  if (recovered && sourceChanged)
    workspace_->reportDocumentOperation(
      "Черновик восстановлен; исходный файл изменился, поэтому требуется новый путь сохранения");
}

/// Выполняет пакет только в потоке владельца и передаёт результат общей процедуре публикации.
void PolygonDocumentController::editDocument(const aipackaging::editor::EditorCommandBatch & batch)
{
  if (!session_ || activeDocument_->state().running)
    return;
  aipackaging::editor::PolygonEditorSession candidate = *session_;
  const aipackaging::editor::EditorCommandResult result = candidate.execute(batch);
  publishEdited(result, std::move(candidate));
}

/// Восстанавливает предыдущее состояние через тот же путь локальной и точной проверки.
void PolygonDocumentController::undo()
{
  if (!session_ || activeDocument_->state().running)
    return;
  aipackaging::editor::PolygonEditorSession candidate = *session_;
  const aipackaging::editor::EditorCommandResult result = candidate.undo();
  publishEdited(result, std::move(candidate));
}

/// Восстанавливает повторённое состояние через тот же путь локальной и точной проверки.
void PolygonDocumentController::redo()
{
  if (!session_ || activeDocument_->state().running)
    return;
  aipackaging::editor::PolygonEditorSession candidate = *session_;
  const aipackaging::editor::EditorCommandResult result = candidate.redo();
  publishEdited(result, std::move(candidate));
}

/// Выдаёт служебный идентификатор только пока редактирование текущего документа разрешено.
std::uint64_t PolygonDocumentController::beginGesture() noexcept
{
  if (!session_ || activeDocument_->state().running)
    return 0;
  return session_->beginGesture();
}

/// Возвращает документ к состоянию до жеста и публикует его только при успешной отмене.
void PolygonDocumentController::cancelGesture(std::uint64_t gestureId)
{
  if (!session_ || activeDocument_->state().running)
    return;
  aipackaging::editor::PolygonEditorSession candidate = *session_;
  const aipackaging::editor::EditorCommandResult result = candidate.cancelGesture(gestureId);
  publishEdited(result, std::move(candidate));
}

/// Закрывает возможность дальнейшего объединения с завершённым жестом.
void PolygonDocumentController::finishGesture(std::uint64_t gestureId)
{
  if (session_ && !activeDocument_->state().running)
    session_->finishGesture(gestureId);
}

/// Повторяет точную компиляцию принятой редакции и сохраняет ошибочную геометрию как черновик.
void PolygonDocumentController::publishEdited(const aipackaging::editor::EditorCommandResult & result,
                                              aipackaging::editor::PolygonEditorSession candidate)
{
  if (!result.accepted || !session_)
  {
    if (!result.error.empty())
      workspace_->reportDocumentOperation("Изменение не выполнено: " + result.error);
    return;
  }
  const auto & state = activeDocument_->state();
  PolygonEditableDocumentLoadResult loaded =
    gateway_->compileImported(candidate.document(), state.sourceIdentifier, baseFingerprint_);
  if (!loaded.success)
  {
    workspace_->reportDocumentOperation("Изменение не выполнено: " + loaded.error);
    return;
  }
  loaded.source = state.source;
  loaded.sourceIdentifier = state.sourceIdentifier;
  session_ = std::move(candidate);
  workspace_->acceptEditedDocument(std::move(loaded), result.history, session_->snapshot());
  if (!result.history.dirty)
    invalidateOwnedRecovery();
}

/// Удаляет восстановительный файл после записи пользователем и обновляет карточку.
bool PolygonDocumentController::clearRecoveryAfterSave(const std::string & savedPath)
{
  if (savedPath == autosavePath_)
  {
    inspectRecovery();
    return true;
  }
  if (!ownsRecovery_)
    return true;
  if (draftJobs_)
  {
    invalidateOwnedRecovery();
    return !ownsRecovery_;
  }
  if (recoveryFingerprint_ && gateway_->sourceFingerprint(autosavePath_) != recoveryFingerprint_)
  {
    ownsRecovery_ = false;
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
  ownsRecovery_ = false;
  activeDraftJob_.reset();
  return true;
}
