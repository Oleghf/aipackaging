#include <memory>
#include <utility>

#include <activepolygondocument.h>
#include <gtest/gtest.h>
#include <polygondocumentcontroller.h>
#include <polygonworkspacecontroller.h>

namespace
{
/// Сохраняет последний опубликованный прикладной снимок.
class OutputStub final : public IPolygonWorkspaceOutput
{
public:
  /// Заменяет последний снимок переданным значением.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & value) override { snapshot = value; }
  PolygonWorkspaceSnapshot snapshot;
};

/// Имитирует загрузку, сохранение и освобождение процессных артефактов.
class DocumentGatewayStub final : public IPolygonDocumentGateway
{
public:
  /// Возвращает подготовленный результат либо ошибку для специального пути.
  PolygonDocumentLoadResult load(const std::string & filePath) override
  {
    if (filePath == "bad")
      return {false, "повреждённый документ"};
    PolygonDocumentLoadResult result;
    result.success = true;
    result.document = {nextDocument++};
    result.problemId = filePath;
    result.summary.sheetWidth = 100.0;
    result.summary.parts.push_back({"part", 1, {0, 90}, 20.0, 10.0, 200.0, 0});
    result.scene.sheetWidth = 100.0;
    result.unplacedInstances = {"part #0"};
    return result;
  }

  /// Запоминает решение и возвращает настраиваемый результат записи.
  PolygonDocumentOperationResult save(const std::string &, PolygonSolutionHandle solution) override
  {
    saved = solution;
    return saveSucceeds ? PolygonDocumentOperationResult{true, {}} : PolygonDocumentOperationResult{false, "запись запрещена"};
  }

  /// Запоминает освобождённую задачу.
  void release(PolygonDocumentHandle document) noexcept override
  {
    ++releasedDocumentCount;
    lastReleasedDocument = document;
  }
  /// Запоминает освобождённое решение.
  void release(PolygonSolutionHandle solution) noexcept override
  {
    ++releasedSolutionCount;
    lastReleasedSolution = solution;
  }

  std::uint64_t nextDocument = 1;
  bool saveSucceeds = true;
  std::optional<PolygonSolutionHandle> saved;
  std::size_t releasedDocumentCount = 0;
  std::size_t releasedSolutionCount = 0;
  PolygonDocumentHandle lastReleasedDocument;
  PolygonSolutionHandle lastReleasedSolution;
};

/// Имитирует хранение редактируемого документа и автоматического черновика.
class EditableDocumentGatewayStub final : public IPolygonEditableDocumentGateway
{
public:
  /// Возвращает подготовленный документ либо управляемую ошибку.
  PolygonEditableDocumentLoadResult load(const std::string & filePath) override
  {
    ++loadCount;
    if (filePath == "bad")
      return {false, "повреждённый документ"};
    PolygonEditableDocumentLoadResult result;
    result.success = true;
    result.source = PolygonDocumentSource::ProblemFile;
    result.sourceIdentifier = filePath;
    result.problemId = filePath;
    result.document.problemId = filePath;
    result.document.sheet.width = 100.0;
    result.document.sheet.height = 80.0;
    PolygonDocumentLoadResult compiled;
    compiled.success = true;
    compiled.document = {nextDocument++};
    compiled.problemId = filePath;
    compiled.summary.sheetWidth = 100.0;
    compiled.scene.sheetWidth = 100.0;
    result.summary = compiled.summary;
    result.scene = compiled.scene;
    result.compiled = std::move(compiled);
    return result;
  }

  /// Возвращает управляемый восстановленный документ.
  PolygonEditableDocumentLoadResult loadRecovery(const std::string & filePath) override
  {
    PolygonEditableDocumentLoadResult result = load(filePath);
    result.source = PolygonDocumentSource::RecoveredDraft;
    result.sourceIdentifier = "original.json";
    result.generation = 4;
    if (invalidRecovery)
    {
      result.compiled.reset();
      result.diagnostics.push_back({aipackaging::editor::DocumentDiagnosticCode::OpenPath,
                                    aipackaging::editor::DiagnosticSeverity::Error,
                                    {},
                                    "Контур открыт"});
    }
    return result;
  }

  /// Запоминает сохранение строгой задачи.
  PolygonDocumentOperationResult saveProblem(const std::string & filePath,
                                             const aipackaging::editor::EditablePolygonDocument &) override
  {
    savedProblem = filePath;
    return saveSucceeds ? PolygonDocumentOperationResult{true, {}} : PolygonDocumentOperationResult{false, "отказ"};
  }

  /// Запоминает поколение и путь сохранённого черновика.
  PolygonDocumentOperationResult saveDraft(const std::string & filePath, const aipackaging::editor::EditablePolygonDocument &,
                                           PolygonDocumentSource source, const std::string & sourceIdentifier,
                                           std::uint64_t generation,
                                           std::optional<PolygonSourceFingerprint> baseFingerprint) override
  {
    savedDraft = filePath;
    savedSource = source;
    savedSourceIdentifier = sourceIdentifier;
    savedGeneration = generation;
    savedBaseFingerprint = baseFingerprint;
    return saveSucceeds ? PolygonDocumentOperationResult{true, {}} : PolygonDocumentOperationResult{false, "отказ"};
  }

  /// Возвращает управляемый отпечаток сохранённого источника.
  std::optional<PolygonSourceFingerprint> sourceFingerprint(const std::string &) override { return savedFingerprint; }

  /// Возвращает подготовленную карточку автоматического восстановления.
  PolygonRecoveryCandidate inspectRecovery(const std::string & filePath) override
  {
    PolygonRecoveryCandidate result = recovery;
    result.autosavePath = filePath;
    return result;
  }

  /// Запоминает удаление автоматического файла.
  PolygonDocumentOperationResult removeRecovery(const std::string &) override
  {
    ++removeCount;
    recovery = {};
    return {true, {}};
  }

  std::uint64_t nextDocument = 50;
  int loadCount = 0;
  int removeCount = 0;
  bool invalidRecovery = false;
  bool saveSucceeds = true;
  std::string savedProblem;
  std::string savedDraft;
  PolygonDocumentSource savedSource = PolygonDocumentSource::None;
  std::string savedSourceIdentifier;
  std::uint64_t savedGeneration = 0;
  std::optional<PolygonSourceFingerprint> savedBaseFingerprint;
  std::optional<PolygonSourceFingerprint> savedFingerprint = PolygonSourceFingerprint{77, 88};
  PolygonRecoveryCandidate recovery{true, true, false, {}, "original.json", "2026-09-26T12:00:00Z", {}};
};

/// Имитирует средство запуска с ручной доставкой событий.
class JobRunnerStub final : public INestingJobRunner
{
public:
  /// Сохраняет запрос и функции событий, не выполняя работу автоматически.
  std::optional<NestingJobHandle> start(PolygonDocumentHandle document, const NestingRunRequest & request,
                                        NestingJobCallbacks value, std::string & error) override
  {
    if (rejectStart)
    {
      error = "нет свободного исполнителя";
      return std::nullopt;
    }
    ++startCount;
    startedDocument = document;
    startedRequest = request;
    callbacks = std::move(value);
    current = {nextJob++};
    return current;
  }

  /// Запоминает запрос отмены указанной работы.
  void cancel(NestingJobHandle job) noexcept override { cancelled = job; }

  /// Доставляет подготовленный результат с выбранным идентификатором.
  void complete(NestingRunResult result, NestingJobHandle job = {})
  {
    callbacks.completed(job ? job : current, std::move(result));
  }

  /// Доставляет подготовленную ошибку текущей работы.
  void fail(const std::string & error) { callbacks.failed(current, error); }

  std::uint64_t nextJob = 1;
  int startCount = 0;
  bool rejectStart = false;
  PolygonDocumentHandle startedDocument;
  NestingRunRequest startedRequest;
  NestingJobHandle current;
  std::optional<NestingJobHandle> cancelled;
  NestingJobCallbacks callbacks;
};

/// Имитирует строгую загрузку и освобождение внешнего комплекта модели.
class ModelGatewayStub final : public IPolygonModelJobRunner
{
public:
  /// Сохраняет функции событий для управляемого завершения проверки.
  std::optional<PolygonModelJobHandle> start(const std::string & directory, PolygonModelJobCallbacks value,
                                             std::string & error) override
  {
    if (rejectStart)
    {
      error = "нет свободного исполнителя";
      return std::nullopt;
    }
    requestedDirectory = directory;
    callbacks = std::move(value);
    current = {nextJob++};
    return current;
  }

  /// Запоминает отмену актуальной проверки.
  void cancel(PolygonModelJobHandle job) noexcept override { cancelled = job; }

  /// Запоминает освобождённую модель.
  void release(PolygonModelHandle model) noexcept override
  {
    ++releasedCount;
    lastReleased = model;
  }

  /// Доставляет успешный результат актуальной проверки.
  void complete(std::string id = "policy")
  {
    callbacks.completed(current, {true, {}, PolygonModelHandle{nextModel++}, std::move(id), "0123456789abcdef"});
  }
  /// Доставляет ошибку актуальной проверки.
  void fail(const std::string & error) { callbacks.failed(current, error); }
  /// Доставляет подтверждение отмены актуальной проверки.
  void completeCancellation() { callbacks.cancelled(current); }

  std::uint64_t nextJob = 1;
  std::uint64_t nextModel = 1;
  bool rejectStart = false;
  std::string requestedDirectory;
  PolygonModelJobHandle current;
  std::optional<PolygonModelJobHandle> cancelled;
  PolygonModelJobCallbacks callbacks;
  std::size_t releasedCount = 0;
  PolygonModelHandle lastReleased;
};

/// Создаёт контроллер и возвращает совместно используемые тестовые порты.
std::shared_ptr<PolygonWorkspaceController> makeController(std::shared_ptr<OutputStub> & output,
                                                           std::shared_ptr<DocumentGatewayStub> & documents,
                                                           std::shared_ptr<JobRunnerStub> & jobs,
                                                           std::shared_ptr<ModelGatewayStub> models = {})
{
  output = std::make_shared<OutputStub>();
  documents = std::make_shared<DocumentGatewayStub>();
  jobs = std::make_shared<JobRunnerStub>();
  return std::make_shared<PolygonWorkspaceController>(output, documents, jobs, std::move(models));
}

/// Создаёт полный проверенный результат для тестов автомата состояния.
NestingRunResult solvedResult(std::uint64_t handle = 7)
{
  NestingRunResult result;
  result.completion = NestingCompletion::Solved;
  result.partial = false;
  result.implementationName = "area-left-bottom";
  result.solutionStatus = "solved";
  result.solution = PolygonSolutionHandle{handle};
  result.objective.placedParts = 1;
  result.objective.totalParts = 1;
  result.scene.placements.push_back({});
  return result;
}

/// Создаёт два согласованных контроллера поверх общих тестовых портов.
std::pair<std::shared_ptr<PolygonWorkspaceController>, std::shared_ptr<PolygonDocumentController>>
makeDocumentControllers(std::shared_ptr<OutputStub> & output, std::shared_ptr<DocumentGatewayStub> & documents,
                        std::shared_ptr<EditableDocumentGatewayStub> & editable, std::shared_ptr<JobRunnerStub> & jobs)
{
  output = std::make_shared<OutputStub>();
  documents = std::make_shared<DocumentGatewayStub>();
  editable = std::make_shared<EditableDocumentGatewayStub>();
  jobs = std::make_shared<JobRunnerStub>();
  auto active = std::make_shared<ActivePolygonDocument>();
  auto workspace = std::make_shared<PolygonWorkspaceController>(output, documents, jobs, nullptr, active);
  auto document = std::make_shared<PolygonDocumentController>(editable, workspace, active, "autosave.aipdraft.json");
  return {workspace, document};
}
} // namespace

/// Проверяет полный жизненный цикл документа и устаревание решения после изменения.
TEST(ActivePolygonDocument, TracksDirtyValidityRunAndStaleSolution)
{
  ActivePolygonDocument document;
  ASSERT_TRUE(document.replace({7}, PolygonDocumentSource::ProblemFile, "problem.json", true));
  EXPECT_FALSE(document.state().dirty);
  EXPECT_TRUE(document.state().valid);
  ASSERT_TRUE(document.beginRun());
  EXPECT_TRUE(document.state().running);
  document.finishRun(true);
  EXPECT_TRUE(document.state().hasSolution);

  document.markChanged(false);
  EXPECT_TRUE(document.state().dirty);
  EXPECT_FALSE(document.state().valid);
  EXPECT_FALSE(document.state().hasSolution);
  EXPECT_TRUE(document.state().solutionStale);
  EXPECT_FALSE(document.beginRun());

  document.setValid(true);
  document.markSaved(PolygonDocumentSource::Draft, "draft.json");
  EXPECT_FALSE(document.state().dirty);
  EXPECT_EQ(document.state().source, PolygonDocumentSource::Draft);
  EXPECT_TRUE(document.beginRun());
}

/// Проверяет запрет замены и очистки документа до завершения активной работы.
TEST(ActivePolygonDocument, RequiresRunCompletionBeforeReplacement)
{
  ActivePolygonDocument document;
  ASSERT_TRUE(document.replace({1}, PolygonDocumentSource::ProblemFile, "first.json", true));
  ASSERT_TRUE(document.beginRun());
  EXPECT_FALSE(document.replace({2}, PolygonDocumentSource::Imported, "second.dxf", true));
  EXPECT_FALSE(document.clear());
  ASSERT_TRUE(document.state().handle.has_value());
  EXPECT_EQ(document.state().handle->value, 1);

  document.finishRun(false);
  EXPECT_TRUE(document.replace({2}, PolygonDocumentSource::Imported, "second.dxf", true));
  ASSERT_TRUE(document.state().handle.has_value());
  EXPECT_EQ(document.state().handle->value, 2);
}

/// Проверяет полный прикладной цикл без решателя, Qt и файловой системы.
TEST(PolygonWorkspaceController, LoadsRunsAndSavesThroughPorts)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  const auto actions = controller->actions();
  actions.openProblem("problem");
  ASSERT_EQ(output->snapshot.state, PolygonWorkspaceState::Ready);
  actions.start({});
  ASSERT_EQ(output->snapshot.state, PolygonWorkspaceState::Running);
  jobs->complete(solvedResult());
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Completed);
  EXPECT_TRUE(output->snapshot.canSave);
  actions.saveSolution("solution");
  ASSERT_TRUE(documents->saved.has_value());
  EXPECT_EQ(documents->saved.value_or(PolygonSolutionHandle{}).value, 7);
}

/// Проверяет сохранение прежнего документа и сцены после ошибки загрузки.
TEST(PolygonWorkspaceController, KeepsPreviousSceneAfterLoadError)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("valid");
  controller->openProblem("bad");
  EXPECT_EQ(output->snapshot.problemId, "valid");
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Ready);
  EXPECT_NE(output->snapshot.statusText.find("Ошибка загрузки"), std::string::npos);
}

/// Проверяет запрет параллельного запуска и несохраняемость отменённого результата.
TEST(PolygonWorkspaceController, CancelsWithoutStartingSecondRun)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  controller->start({});
  controller->start({});
  EXPECT_EQ(jobs->startCount, 1);
  controller->cancel();
  EXPECT_EQ(jobs->cancelled, jobs->current);
  NestingRunResult cancelled;
  cancelled.completion = NestingCompletion::Cancelled;
  cancelled.partial = true;
  jobs->complete(std::move(cancelled));
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Cancelled);
  EXPECT_FALSE(output->snapshot.canSave);
}

/// Проверяет сохранение обычного частичного результата после ограничения времени.
TEST(PolygonWorkspaceController, KeepsValidatedTimedOutPartialSaveable)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  controller->start({});
  NestingRunResult result = solvedResult();
  result.completion = NestingCompletion::TimedOut;
  result.partial = true;
  jobs->complete(std::move(result));
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Completed);
  EXPECT_TRUE(output->snapshot.canSave);
  EXPECT_TRUE(output->snapshot.partial);
}

/// Проверяет отклонение недоступного нейросетевого режима без обращения к средству запуска.
TEST(PolygonWorkspaceController, RejectsUnavailableMethod)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  NestingRunRequest request;
  request.method = NestingMethod::Neural;
  controller->start(request);
  EXPECT_EQ(jobs->startCount, 0);
  EXPECT_NE(output->snapshot.statusText.find("недоступен"), std::string::npos);
}

/// Проверяет сохранение последней корректной сцены при ошибке следующего запуска.
TEST(PolygonWorkspaceController, KeepsValidatedSolutionAfterLaterFailure)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  controller->start({});
  jobs->complete(solvedResult());
  controller->start({});
  jobs->fail("сбой");
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Error);
  EXPECT_TRUE(output->snapshot.canSave);
  EXPECT_EQ(output->snapshot.scene.placements.size(), 1);
}

/// Проверяет освобождение результата запоздалого события без изменения текущего состояния.
TEST(PolygonWorkspaceController, IgnoresAndReleasesStaleResult)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  controller->start({});
  jobs->complete(solvedResult(19), NestingJobHandle{999});
  EXPECT_EQ(output->snapshot.state, PolygonWorkspaceState::Running);
  ASSERT_EQ(documents->releasedSolutionCount, 1);
  EXPECT_EQ(documents->lastReleasedSolution.value, 19);
}

/// Проверяет диагностируемую ошибку сохранения без потери проверенного результата.
TEST(PolygonWorkspaceController, ReportsSaveFailureAndKeepsResult)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("problem");
  controller->start({});
  jobs->complete(solvedResult());
  documents->saveSucceeds = false;
  controller->saveSolution("solution");
  EXPECT_TRUE(output->snapshot.canSave);
  EXPECT_NE(output->snapshot.statusText.find("Ошибка сохранения"), std::string::npos);
}

/// Проверяет передачу проверенной модели нейросетевому запуску и её замену.
TEST(PolygonWorkspaceController, LoadsReplacesAndUsesModel)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  auto models = std::make_shared<ModelGatewayStub>();
  const auto controller = makeController(output, documents, jobs, models);
  controller->openProblem("problem");
  controller->openModel("first");
  ASSERT_EQ(output->snapshot.modelState, PolygonModelState::Loading);
  models->complete();
  ASSERT_TRUE(output->snapshot.modelReady);
  EXPECT_EQ(output->snapshot.modelId, "policy");

  NestingRunRequest request;
  request.method = NestingMethod::Neural;
  controller->start(request);
  ASSERT_EQ(jobs->startCount, 1);
  ASSERT_TRUE(jobs->startedRequest.model.has_value());
  EXPECT_EQ(jobs->startedRequest.model->value, 1);
  jobs->complete(solvedResult());

  controller->openModel("second");
  models->complete("policy-2");
  EXPECT_EQ(models->releasedCount, 1);
  EXPECT_EQ(models->lastReleased.value, 1);
  EXPECT_EQ(output->snapshot.modelSha256, "0123456789abcdef");
}

/// Проверяет сохранение прежней модели и сцены после ошибки следующей загрузки.
TEST(PolygonWorkspaceController, KeepsPreviousModelAfterLoadFailure)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  auto models = std::make_shared<ModelGatewayStub>();
  const auto controller = makeController(output, documents, jobs, models);
  controller->openProblem("problem");
  controller->openModel("valid-model");
  models->complete();
  controller->openModel("bad-model");
  models->fail("повреждённая модель");
  EXPECT_TRUE(output->snapshot.modelReady);
  EXPECT_EQ(output->snapshot.modelId, "policy");
  EXPECT_NE(output->snapshot.modelStatusText.find("Ошибка"), std::string::npos);
  EXPECT_EQ(models->releasedCount, 0);
}

/// Проверяет отмену и освобождение результата запоздалой проверки модели.
TEST(PolygonWorkspaceController, CancelsModelLoadAndRejectsStaleResult)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  auto models = std::make_shared<ModelGatewayStub>();
  const auto controller = makeController(output, documents, jobs, models);
  controller->openModel("slow-model");
  const PolygonModelJobHandle oldJob = models->current;
  EXPECT_EQ(output->snapshot.modelState, PolygonModelState::Loading);
  EXPECT_TRUE(output->snapshot.canCancelModelLoad);
  controller->cancelModelLoad();
  ASSERT_TRUE(models->cancelled.has_value());
  EXPECT_EQ(models->cancelled->value, oldJob.value);
  models->completeCancellation();
  EXPECT_EQ(output->snapshot.modelState, PolygonModelState::NotSelected);

  controller->openModel("new-model");
  const PolygonModelJobHandle current = models->current;
  models->callbacks.completed(oldJob, {true, {}, PolygonModelHandle{77}, "stale", "hash"});
  EXPECT_EQ(models->releasedCount, 1);
  EXPECT_EQ(models->lastReleased.value, 77U);
  models->callbacks.completed(current, {true, {}, PolygonModelHandle{78}, "current", "hash-2"});
  EXPECT_EQ(output->snapshot.modelId, "current");
}

/// Проверяет публикацию нейтральных сведений документа и его источника.
TEST(PolygonWorkspaceController, PublishesNeutralDocumentSummary)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto controller = makeController(output, documents, jobs);
  controller->openProblem("folder/problem.json");
  EXPECT_EQ(output->snapshot.document.sourceIdentifier, "folder/problem.json");
  EXPECT_DOUBLE_EQ(output->snapshot.document.sheetWidth, 100.0);
  ASSERT_EQ(output->snapshot.document.parts.size(), 1U);
  EXPECT_EQ(output->snapshot.document.parts.front().id, "part");
  EXPECT_TRUE(output->snapshot.documentValid);
}

/// Проверяет открытие и сохранение задачи через отдельный контроллер документа.
TEST(PolygonDocumentController, OpensAndSavesWithoutFileSystemTypesInApplication)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  actions.openProblem("problem.json");
  EXPECT_TRUE(output->snapshot.hasDocument);
  EXPECT_TRUE(output->snapshot.documentValid);
  EXPECT_EQ(output->snapshot.documentSource, PolygonDocumentSource::ProblemFile);
  actions.saveDocument("saved.json", false);
  EXPECT_EQ(editable->savedProblem, "saved.json");
  EXPECT_FALSE(output->snapshot.documentDirty);
}

/// Проверяет отмену текущей работы до загрузки следующего документа.
TEST(PolygonDocumentController, WaitsForRunCompletionBeforeReplacement)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  actions.openProblem("first.json");
  actions.start({});
  actions.openProblem("second.json");
  EXPECT_EQ(editable->loadCount, 1);
  ASSERT_TRUE(jobs->cancelled.has_value());
  NestingRunResult cancelled;
  cancelled.completion = NestingCompletion::Cancelled;
  cancelled.partial = true;
  jobs->complete(std::move(cancelled));
  EXPECT_EQ(editable->loadCount, 2);
  EXPECT_EQ(output->snapshot.problemId, "second.json");
}

/// Проверяет восстановление некорректного черновика и последовательные поколения автосохранения.
TEST(PolygonDocumentController, RestoresInvalidDraftAndAutosavesDirtyState)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  editable->invalidRecovery = true;
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  controller->inspectRecovery();
  EXPECT_TRUE(output->snapshot.recovery.present);
  actions.restoreRecovery();
  EXPECT_TRUE(output->snapshot.documentDirty);
  EXPECT_FALSE(output->snapshot.documentValid);
  EXPECT_FALSE(output->snapshot.canRun);
  EXPECT_TRUE(output->snapshot.canSaveDocument);
  actions.autosaveDocument();
  EXPECT_EQ(editable->savedDraft, "autosave.aipdraft.json");
  EXPECT_EQ(editable->savedGeneration, 5U);
}

/// Проверяет автосохранение неизменяемого снимка во время фонового расчёта.
TEST(PolygonDocumentController, FlushesDirtyDraftWhileRunIsActive)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  actions.restoreRecovery();
  ASSERT_TRUE(output->snapshot.documentDirty);
  ASSERT_TRUE(output->snapshot.documentValid);
  actions.start({});
  ASSERT_TRUE(output->snapshot.canCancel);
  actions.autosaveDocument();
  EXPECT_EQ(editable->savedDraft, "autosave.aipdraft.json");
  EXPECT_EQ(editable->savedSource, PolygonDocumentSource::RecoveredDraft);
  EXPECT_EQ(editable->savedSourceIdentifier, "original.json");
}

/// Проверяет сохранение признака изменения при отказе автоматической записи.
TEST(PolygonDocumentController, KeepsDirtyStateAfterAutosaveFailure)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  actions.restoreRecovery();
  editable->saveSucceeds = false;
  actions.autosaveDocument();
  EXPECT_TRUE(output->snapshot.documentDirty);
  EXPECT_NE(output->snapshot.statusText.find("Ошибка автосохранения"), std::string::npos);
}

/// Проверяет сохранение прежнего документа и сцены после ошибки следующего открытия.
TEST(PolygonDocumentController, KeepsPreviousDocumentAfterLoadFailure)
{
  std::shared_ptr<OutputStub> output;
  std::shared_ptr<DocumentGatewayStub> documents;
  std::shared_ptr<EditableDocumentGatewayStub> editable;
  std::shared_ptr<JobRunnerStub> jobs;
  const auto [workspace, controller] = makeDocumentControllers(output, documents, editable, jobs);
  PolygonWorkspaceActions actions = workspace->actions();
  controller->bindActions(actions);
  actions.openProblem("first.json");
  const double sheetWidth = output->snapshot.scene.sheetWidth;
  actions.openProblem("bad");
  EXPECT_EQ(output->snapshot.problemId, "first.json");
  EXPECT_DOUBLE_EQ(output->snapshot.scene.sheetWidth, sheetWidth);
  EXPECT_NE(output->snapshot.statusText.find("Ошибка загрузки"), std::string::npos);
}
