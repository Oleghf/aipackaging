#include <chrono>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <polygondesktopinfrastructure.h>
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
#include <aipackaging/inference/polygon_onnx.h>
#endif

/// Хранит авторитетную задачу и подготовленную геометрию только внутри инфраструктуры.
struct PolygonArtifactStore::DocumentRecord
{
  aipackaging::solver::PolygonProblem problem;
  std::shared_ptr<aipackaging::solver::PolygonEnvironment> environment;
};

/// Содержит синхронизацию, счётчики и таблицы закрытого процессного хранилища.
struct PolygonArtifactStore::Impl
{
  std::mutex mutex;
  std::uint64_t nextDocument = 1;
  std::uint64_t nextSolution = 1;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  std::uint64_t nextModel = 1;
#endif
  std::unordered_map<std::uint64_t, std::shared_ptr<const DocumentRecord>> documents;
  std::unordered_map<std::uint64_t, std::shared_ptr<const aipackaging::solver::PolygonSolution>> solutions;
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
  std::unordered_map<std::uint64_t, std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy>> models;
#endif
};

namespace
{
using namespace aipackaging::solver;

/// Переводит микронную точку кольца в миллиметровую точку представления.
PolygonViewPoint toViewPoint(const PolygonPoint64 & point, std::int64_t offsetX, std::int64_t offsetY)
{
  return {static_cast<double>(point.x + offsetX) / 1000.0, static_cast<double>(point.y + offsetY) / 1000.0};
}

/// Преобразует прикладное имя базового алгоритма в контракт поискового модуля.
SolverKind toSolverKind(BaselineAlgorithm algorithm)
{
  switch (algorithm)
  {
    case BaselineAlgorithm::InputFirstFit:
      return SolverKind::InputFirstFit;
    case BaselineAlgorithm::AreaLeftBottom:
      return SolverKind::AreaLeftBottom;
    case BaselineAlgorithm::MaxSideLeftBottom:
      return SolverKind::MaxSideLeftBottom;
    case BaselineAlgorithm::RandomLeftBottom:
      return SolverKind::RandomLeftBottom;
    case BaselineAlgorithm::Beam:
      return SolverKind::Beam;
  }
  throw std::invalid_argument("Неизвестный базовый алгоритм");
}

/// Преобразует этап поискового модуля в независимый прикладной этап.
NestingProgressStage toProgressStage(SearchProgressStage stage)
{
  switch (stage)
  {
    case SearchProgressStage::Instances:
      return NestingProgressStage::Instances;
    case SearchProgressStage::RandomIterations:
      return NestingProgressStage::RandomIterations;
    case SearchProgressStage::ExpandedStates:
      return NestingProgressStage::ExpandedStates;
  }
  return NestingProgressStage::Instances;
}

/// Преобразует статус решателя в причину завершения пользовательского сценария.
NestingCompletion toCompletion(SolveStatus status, bool cancelled)
{
  if (cancelled)
    return NestingCompletion::Cancelled;
  switch (status)
  {
    case SolveStatus::Solved:
      return NestingCompletion::Solved;
    case SolveStatus::NoSolutionFound:
      return NestingCompletion::NoSolutionFound;
    case SolveStatus::BudgetExhausted:
      return NestingCompletion::BudgetExhausted;
    case SolveStatus::TimedOut:
      return NestingCompletion::TimedOut;
    case SolveStatus::UnsupportedEnvironment:
      return NestingCompletion::UnsupportedEnvironment;
    case SolveStatus::InvalidProblem:
      throw std::runtime_error("Решатель вернул статус некорректной задачи после успешной загрузки");
  }
  throw std::runtime_error("Решатель вернул неизвестный статус");
}

/// Формирует геометрическую сцену и перечень неразмещённых экземпляров.
void buildPresentation(const PolygonArtifactStore::DocumentRecord & record, const PolygonSolution * solution,
                       PolygonSceneView & scene, std::vector<std::string> & unplaced)
{
  scene = {};
  unplaced.clear();
  scene.sheetWidth = record.problem.sheet.width;
  scene.sheetHeight = record.problem.sheet.height;
  scene.sheetMargin = record.problem.manufacturing.sheetMargin;
  std::set<std::pair<std::string, std::uint32_t>> placed;
  if (solution)
  {
    scene.usedLength = static_cast<double>(solution->objective.usedLength) / 1000.0;
    scene.primaryRemnantWidth = static_cast<double>(solution->objective.primaryRemnantWidth) / 1000.0;
    for (const PolygonPlacement & placement : solution->placements)
    {
      const std::size_t instancePosition = record.environment->findInstance(placement.partId, placement.instanceIndex);
      if (instancePosition >= record.environment->instances().size())
        continue;
      const std::size_t partIndex = record.environment->instances()[instancePosition].partIndex;
      const PolygonOrientation * orientation = record.environment->findOrientation(partIndex, placement.rotationDegrees);
      if (!orientation)
        continue;
      PolygonPlacedPartView part;
      part.partId = placement.partId;
      part.instanceIndex = placement.instanceIndex;
      part.colorIndex = partIndex;
      for (const PolygonPoint64 & point : orientation->outer)
        part.outer.push_back(toViewPoint(point, placement.x, placement.y));
      for (const PolygonRing64 & hole : orientation->holes)
      {
        std::vector<PolygonViewPoint> viewHole;
        for (const PolygonPoint64 & point : hole)
          viewHole.push_back(toViewPoint(point, placement.x, placement.y));
        part.holes.push_back(std::move(viewHole));
      }
      scene.placements.push_back(std::move(part));
      placed.emplace(placement.partId, placement.instanceIndex);
    }
  }
  for (const PolygonPartInstance & instance : record.environment->instances())
  {
    const std::string & id = record.problem.parts[instance.partIndex].id;
    if (!placed.contains({id, instance.instanceIndex}))
      unplaced.push_back(id + " #" + std::to_string(instance.instanceIndex));
  }
}

/// Преобразует проверяемое решение ядра в прикладной результат и регистрирует его для сохранения.
NestingRunResult makeRunResult(const std::shared_ptr<PolygonArtifactStore> & store,
                               const std::shared_ptr<const PolygonArtifactStore::DocumentRecord> & record,
                               PolygonDocumentHandle document, PolygonSolution solution, bool cancelled,
                               NestingProvenance provenance, std::string diagnostic = {})
{
  NestingRunResult result;
  result.completion = toCompletion(solution.status, cancelled);
  result.provenance = provenance;
  result.implementationName = solution.solver.name;
  result.diagnostic = std::move(diagnostic);
  result.solutionStatus = toString(solution.status);
  result.partial = !solution.complete();
  result.objective = {static_cast<std::uint64_t>(solution.objective.usedLength),
                      static_cast<std::uint64_t>(solution.objective.primaryRemnantWidth),
                      solution.objective.largestExtraRectangleArea,
                      solution.objective.fragmentationPenalty,
                      solution.objective.placedParts,
                      solution.objective.totalParts,
                      solution.objective.materialUtilization};
  result.metrics = {solution.metrics.candidatesGenerated, solution.metrics.expandedStates, solution.metrics.totalTimeUs};
  buildPresentation(*record, &solution, result.scene, result.unplacedInstances);
  if (!cancelled)
  {
    std::string error;
    result.solution = store->addValidatedSolution(document, std::move(solution), error);
    if (!result.solution)
      throw std::runtime_error("Внутренняя реализация вернула некорректный результат: " + error);
  }
  return result;
}
} // namespace

/// Создаёт закрытую реализацию пустого хранилища.
PolygonArtifactStore::PolygonArtifactStore()
  : impl_(std::make_unique<Impl>())
{
}

/// Уничтожает закрытые записи после завершения всех потребителей.
PolygonArtifactStore::~PolygonArtifactStore() = default;

/// Добавляет неизменяемую запись под новым монотонным идентификатором.
PolygonDocumentHandle PolygonArtifactStore::addDocument(PolygonProblem problem, std::shared_ptr<PolygonEnvironment> environment)
{
  std::lock_guard lock(impl_->mutex);
  const PolygonDocumentHandle handle{impl_->nextDocument++};
  impl_->documents.emplace(handle.value,
                           std::make_shared<const DocumentRecord>(DocumentRecord{std::move(problem), std::move(environment)}));
  return handle;
}

/// Ищет запись под блокировкой и возвращает разделяемое неизменяемое владение.
std::shared_ptr<const PolygonArtifactStore::DocumentRecord> PolygonArtifactStore::document(PolygonDocumentHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->documents.find(handle.value);
  return found == impl_->documents.end() ? nullptr : found->second;
}

/// Проверяет решение относительно исходной задачи до выдачи сохраняемого идентификатора.
std::optional<PolygonSolutionHandle> PolygonArtifactStore::addValidatedSolution(PolygonDocumentHandle documentHandle,
                                                                                PolygonSolution solution, std::string & error)
{
  const auto source = document(documentHandle);
  if (!source)
  {
    error = "Загруженная задача больше недоступна";
    return std::nullopt;
  }
  const ValidationResult validation = validatePolygonSolution(source->problem, solution);
  if (!validation.success)
  {
    error = validation.error;
    return std::nullopt;
  }
  std::lock_guard lock(impl_->mutex);
  const PolygonSolutionHandle handle{impl_->nextSolution++};
  impl_->solutions.emplace(handle.value, std::make_shared<const PolygonSolution>(std::move(solution)));
  return handle;
}

/// Ищет проверенное решение и сохраняет его живым после снятия блокировки.
std::shared_ptr<const PolygonSolution> PolygonArtifactStore::solution(PolygonSolutionHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->solutions.find(handle.value);
  return found == impl_->solutions.end() ? nullptr : found->second;
}

/// Удаляет процессную ссылку на задачу.
void PolygonArtifactStore::release(PolygonDocumentHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->documents.erase(handle.value);
}

/// Удаляет процессную ссылку на решение.
void PolygonArtifactStore::release(PolygonSolutionHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->solutions.erase(handle.value);
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Добавляет неизменяемый исполнитель модели под новым монотонным идентификатором.
PolygonModelHandle PolygonArtifactStore::addModel(std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model)
{
  std::lock_guard lock(impl_->mutex);
  const PolygonModelHandle handle{impl_->nextModel++};
  impl_->models.emplace(handle.value, std::move(model));
  return handle;
}

/// Ищет модель под блокировкой и возвращает разделяемое неизменяемое владение.
std::shared_ptr<const aipackaging::inference::PolygonOnnxPolicy> PolygonArtifactStore::model(PolygonModelHandle handle) const
{
  std::lock_guard lock(impl_->mutex);
  const auto found = impl_->models.find(handle.value);
  return found == impl_->models.end() ? nullptr : found->second;
}

/// Удаляет процессную ссылку на комплект модели.
void PolygonArtifactStore::release(PolygonModelHandle handle) noexcept
{
  std::lock_guard lock(impl_->mutex);
  impl_->models.erase(handle.value);
}

/// Сохраняет хранилище, в котором регистрируются проверенные комплекты модели.
LocalPolygonModelGateway::LocalPolygonModelGateway(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Проверяет комплект через ONNX-адаптер и публикует его только после полного успеха.
PolygonModelLoadResult LocalPolygonModelGateway::load(const std::string & directory)
{
  std::string error;
  std::shared_ptr<aipackaging::inference::PolygonOnnxPolicy> model =
    aipackaging::inference::PolygonOnnxPolicy::Load(directory, error);
  if (!model)
    return {false, error};
  PolygonModelLoadResult result;
  result.success = true;
  result.modelId = model->metadata().modelId;
  result.modelSha256 = model->metadata().modelSha256;
  result.model = store_->addModel(std::move(model));
  return result;
}

/// Передаёт освобождение модели общему процессному хранилищу.
void LocalPolygonModelGateway::release(PolygonModelHandle model) noexcept
{
  store_->release(model);
}
#endif

/// Сохраняет общее хранилище, используемое шлюзом и внутренними реализациями.
LocalPolygonDocumentGateway::LocalPolygonDocumentGateway(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Сначала выполняет строгий разбор и нормализацию, затем публикует процессный документ.
PolygonDocumentLoadResult LocalPolygonDocumentGateway::load(const std::string & filePath)
{
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromFile(filePath);
  if (!loaded.success)
    return {false, loaded.error};
  std::string error;
  std::unique_ptr<PolygonEnvironment> created = PolygonEnvironment::Create(loaded.problem, error);
  if (!created)
    return {false, error};
  std::shared_ptr<PolygonEnvironment> environment(std::move(created));
  PolygonDocumentLoadResult result;
  result.success = true;
  result.problemId = loaded.problem.problemId;
  result.document = store_->addDocument(loaded.problem, environment);
  const auto record = store_->document(result.document);
  buildPresentation(*record, nullptr, result.scene, result.unplacedInstances);
  return result;
}

/// Находит только ранее проверенное решение и передаёт его строгому сериализатору.
PolygonDocumentOperationResult LocalPolygonDocumentGateway::save(const std::string & filePath,
                                                                 PolygonSolutionHandle solutionHandle)
{
  const auto value = store_->solution(solutionHandle);
  if (!value)
    return {false, "Проверенное решение больше недоступно"};
  std::string error;
  return {savePolygonSolutionToFile(filePath, *value, error), error};
}

/// Передаёт освобождение задачи общему хранилищу.
void LocalPolygonDocumentGateway::release(PolygonDocumentHandle document) noexcept
{
  store_->release(document);
}

/// Передаёт освобождение решения общему хранилищу.
void LocalPolygonDocumentGateway::release(PolygonSolutionHandle solution) noexcept
{
  store_->release(solution);
}

/// Сохраняет хранилище, из которого будут извлекаться задачи и куда попадут результаты.
BaselinePolygonBackend::BaselinePolygonBackend(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Преобразует прикладной запрос, запускает поиск и регистрирует результат только после проверки.
NestingRunResult BaselinePolygonBackend::run(PolygonDocumentHandle documentHandle, const NestingRunRequest & request,
                                             const Control & control)
{
  if (request.method != NestingMethod::Baseline)
    throw std::invalid_argument("Запрошенная внутренняя реализация не зарегистрирована");
  const auto record = store_->document(documentHandle);
  if (!record)
    throw std::invalid_argument("Загруженная задача больше недоступна");

  SolverConfig config;
  config.solver = toSolverKind(request.algorithm);
  config.seed = request.seed;
  config.randomIterations = request.randomIterations;
  config.beamWidth = request.beamWidth;
  config.maxExpandedStates = request.maxExpandedStates;
  config.timeoutMs = request.timeoutMs;
  PolygonExecutionControl execution;
  execution.cancellationRequested = control.cancellationRequested;
  execution.progress = [&control](const SearchProgress & progress)
  {
    if (control.progress)
      control.progress({toProgressStage(progress.stage), progress.completed, progress.total, progress.expandedStates});
  };
  PolygonSolverExecutionResult solved = runPolygonProblem(record->problem, config, execution);

  return makeRunResult(store_, record, documentHandle, std::move(solved.solution), solved.cancelled, NestingProvenance::Baseline);
}

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
/// Сохраняет хранилище задач, моделей и независимо проверенных решений.
OnnxPolygonBackend::OnnxPolygonBackend(std::shared_ptr<PolygonArtifactStore> store)
  : store_(std::move(store))
{
}

/// Преобразует прикладной запрос в контракт ONNX и регистрирует только проверенный итог.
NestingRunResult OnnxPolygonBackend::run(PolygonDocumentHandle documentHandle, const NestingRunRequest & request,
                                         const Control & control)
{
  if (request.method == NestingMethod::Baseline)
    throw std::invalid_argument("Базовый запрос передан нейросетевой внутренней реализации");
  if (!request.model)
    throw std::invalid_argument("В запросе отсутствует проверенная полигональная модель");
  const auto record = store_->document(documentHandle);
  const auto model = store_->model(*request.model);
  if (!record)
    throw std::invalid_argument("Загруженная задача больше недоступна");
  if (!model)
    throw std::invalid_argument("Проверенная полигональная модель больше недоступна");
  aipackaging::inference::PolygonPolicyConfig config;
  config.mode = request.neuralSelection == NeuralSelectionMode::Greedy ? aipackaging::inference::PolygonPolicyMode::Greedy
                                                                       : aipackaging::inference::PolygonPolicyMode::BestOf;
  config.seed = request.seed;
  config.rollouts = request.neuralRollouts;
  config.timeoutMs = request.timeoutMs;
  aipackaging::inference::PolygonPolicyControl policyControl;
  policyControl.cancellationRequested = control.cancellationRequested;
  policyControl.progress = [&control](std::size_t completed, std::size_t total)
  {
    if (control.progress)
      control.progress({NestingProgressStage::NeuralRollouts, completed, total, 0});
  };
  aipackaging::inference::PolygonPolicyExecutionResult executed;
  if (request.method == NestingMethod::Hybrid)
  {
    SolverConfig fallback;
    fallback.solver = SolverKind::RandomLeftBottom;
    fallback.seed = request.seed;
    fallback.randomIterations = request.fallbackRandomIterations;
    fallback.beamWidth = request.beamWidth;
    fallback.maxExpandedStates = request.maxExpandedStates;
    fallback.timeoutMs = request.timeoutMs;
    executed = model->runHybrid(record->problem, config, fallback, policyControl);
  }
  else
    executed = model->run(record->problem, config, policyControl);
  const NestingProvenance provenance = executed.fallbackUsed                   ? NestingProvenance::HybridFallback
                                     : request.method == NestingMethod::Hybrid ? NestingProvenance::Hybrid
                                                                               : NestingProvenance::Neural;
  return makeRunResult(store_, record, documentHandle, std::move(executed.solution), executed.cancelled, provenance,
                       std::move(executed.warning));
}
#endif

/// Сохраняет внутренние реализации, не раскрывая их прикладному контроллеру.
PolygonBackendRouter::PolygonBackendRouter(std::shared_ptr<IPolygonNestingBackend> baseline,
                                           std::shared_ptr<IPolygonNestingBackend> neural)
  : baseline_(std::move(baseline))
  , neural_(std::move(neural))
{
}

/// Направляет базовый запрос базовой реализации, а остальные — явно зарегистрированной нейросетевой.
NestingRunResult PolygonBackendRouter::run(PolygonDocumentHandle document, const NestingRunRequest & request,
                                           const Control & control)
{
  if (request.method == NestingMethod::Baseline)
    return baseline_->run(document, request, control);
  if (!neural_)
    throw std::invalid_argument("Эта сборка не содержит внутреннюю реализацию ONNX");
  return neural_->run(document, request, control);
}

/// Сохраняет зависимости; поток создаётся только при первом запуске.
StdThreadNestingJobRunner::StdThreadNestingJobRunner(std::shared_ptr<IPolygonNestingBackend> backend,
                                                     std::shared_ptr<IApplicationDispatcher> dispatcher)
  : backend_(std::move(backend))
  , dispatcher_(std::move(dispatcher))
{
}

/// Совместно останавливает активную работу и присоединяет поток до уничтожения зависимостей.
StdThreadNestingJobRunner::~StdThreadNestingJobRunner()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    worker_.join();
  }
}

/// Завершает предыдущий поток, создаёт идентификатор и запускает новую работу.
std::optional<NestingJobHandle> StdThreadNestingJobRunner::start(PolygonDocumentHandle document,
                                                                 const NestingRunRequest & request, NestingJobCallbacks callbacks,
                                                                 std::string & error)
{
  std::lock_guard lock(mutex_);
  if (activeJob_)
  {
    error = "Другая работа уже выполняется";
    return std::nullopt;
  }
  if (worker_.joinable())
    worker_.join();
  const NestingJobHandle job{nextJob_++};
  activeJob_ = job;
  const auto backend = backend_;
  const auto dispatcher = dispatcher_;
  worker_ = std::jthread(
    [this, backend, dispatcher, document, request, callbacks = std::move(callbacks),
     job](const std::stop_token & stopToken) mutable
    {
      auto lastProgress = std::chrono::steady_clock::time_point::min();
      IPolygonNestingBackend::Control control;
      control.cancellationRequested = [stopToken]()
      {
        return stopToken.stop_requested();
      };
      control.progress = [dispatcher, callbacks, job, &lastProgress](const NestingProgress & progress) mutable
      {
        const auto now = std::chrono::steady_clock::now();
        const bool finalUpdate = progress.total > 0 && progress.completed >= progress.total;
        if (!finalUpdate && lastProgress != std::chrono::steady_clock::time_point::min() &&
            now - lastProgress < std::chrono::milliseconds(100))
          return;
        lastProgress = now;
        if (callbacks.progress)
          dispatcher->post([callback = callbacks.progress, job, progress]() { callback(job, progress); });
      };
      try
      {
        NestingRunResult result = backend->run(document, request, control);
        if (callbacks.completed)
          dispatcher->post([callback = callbacks.completed, job, result = std::move(result)]() mutable
                           { callback(job, std::move(result)); });
      }
      catch (const std::exception & exception)
      {
        if (callbacks.failed)
        {
          const std::string message = exception.what();
          // Функция прикладного контроллера не бросает исключений; это контракт границы доставки.
          // NOLINTNEXTLINE(bugprone-exception-escape)
          dispatcher->post([callback = callbacks.failed, job, message]()
                           { callback(job, message); }); // NOLINT(bugprone-exception-escape)
        }
      }
      std::lock_guard finishLock(mutex_);
      if (activeJob_ == job)
        activeJob_.reset();
    });
  return job;
}

/// Сверяет идентификатор под блокировкой и запрашивает остановку соответствующего потока.
void StdThreadNestingJobRunner::cancel(NestingJobHandle job) noexcept
{
  std::lock_guard lock(mutex_);
  if (activeJob_ == job && worker_.joinable())
    worker_.request_stop();
}
