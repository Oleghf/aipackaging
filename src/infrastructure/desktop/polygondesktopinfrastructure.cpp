#include <chrono>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <polygondesktopinfrastructure.h>

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
  std::unordered_map<std::uint64_t, std::shared_ptr<const DocumentRecord>> documents;
  std::unordered_map<std::uint64_t, std::shared_ptr<const aipackaging::solver::PolygonSolution>> solutions;
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

  NestingRunResult result;
  result.completion = toCompletion(solved.solution.status, solved.cancelled);
  result.provenance = NestingProvenance::Baseline;
  result.implementationName = solved.solution.solver.name;
  result.solutionStatus = toString(solved.solution.status);
  result.partial = !solved.solution.complete();
  result.objective = {static_cast<std::uint64_t>(solved.solution.objective.usedLength),
                      static_cast<std::uint64_t>(solved.solution.objective.primaryRemnantWidth),
                      solved.solution.objective.largestExtraRectangleArea,
                      solved.solution.objective.fragmentationPenalty,
                      solved.solution.objective.placedParts,
                      solved.solution.objective.totalParts,
                      solved.solution.objective.materialUtilization};
  result.metrics = {solved.solution.metrics.candidatesGenerated, solved.solution.metrics.expandedStates,
                    solved.solution.metrics.totalTimeUs};
  buildPresentation(*record, &solved.solution, result.scene, result.unplacedInstances);
  if (!solved.cancelled)
  {
    std::string error;
    result.solution = store_->addValidatedSolution(documentHandle, std::move(solved.solution), error);
    if (!result.solution)
      throw std::runtime_error("Решатель вернул некорректный результат: " + error);
  }
  return result;
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
