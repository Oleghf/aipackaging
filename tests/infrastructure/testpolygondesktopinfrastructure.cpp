#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_io.h>
#include <aipackaging/nesting/polygon_solver.h>
#include <gtest/gtest.h>
#include <nesting_job_runner.h>
#include <polygon_artifact_store.h>
#include <polygon_backends.h>
#include <polygon_document_gateway.h>
#include <polygon_model_jobs.h>

namespace
{
using namespace aipackaging::solver;

/// Создаёт прямоугольный аналитический путь.
PolygonPath rectangle(double width, double height)
{
  PolygonPath result;
  result.start = {0.0, 0.0};
  result.segments = {{PolygonSegmentKind::Line, {width, 0.0}},
                     {PolygonSegmentKind::Line, {width, height}},
                     {PolygonSegmentKind::Line, {0.0, height}},
                     {PolygonSegmentKind::Line, {0.0, 0.0}}};
  return result;
}

/// Создаёт минимальную задачу с двумя экземплярами.
PolygonProblem testProblem()
{
  PolygonProblem result;
  result.problemId = "a6-infrastructure";
  result.sheet = {100.0, 60.0, "mm"};
  result.manufacturing = {2.0, 1.0, 0.2, 0.05};
  PolygonPart part;
  part.id = "part";
  part.quantity = 2;
  part.outer = rectangle(30.0, 20.0);
  part.allowedRotations = {0, 90};
  result.parts.push_back(std::move(part));
  return result;
}

/// Записывает задачу во временный JSON-файл.
std::filesystem::path writeProblem()
{
  const auto path = std::filesystem::temp_directory_path() / "aipackaging-a6-problem.json";
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << savePolygonProblemToText(testProblem());
  return path;
}

/// Накапливает функции до явного выполнения в тестовом потоке.
class QueueDispatcher final : public IApplicationDispatcher
{
public:
  /// Добавляет функцию в защищённую очередь либо сообщает об отказе выделения памяти.
  bool post(std::function<void()> callback) noexcept override
  {
    try
    {
      std::lock_guard lock(mutex_);
      queue_.push_back(std::move(callback));
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  /// Выполняет накопленные функции и возвращает их число.
  std::size_t drain()
  {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard lock(mutex_);
      pending.swap(queue_);
    }
    for (auto & callback : pending)
      callback();
    return pending.size();
  }

  /// Удаляет накопленные события без выполнения и возвращает их число.
  std::size_t discard()
  {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard lock(mutex_);
      pending.swap(queue_);
    }
    return pending.size();
  }

  /// Возвращает текущее число накопленных событий.
  std::size_t size()
  {
    std::lock_guard lock(mutex_);
    return queue_.size();
  }

private:
  std::mutex mutex_;
  std::deque<std::function<void()>> queue_;
};

/// Удерживает первый вызов постановки после помещения события в очередь.
class PausingDispatcher final : public IApplicationDispatcher
{
public:
  /// Ставит событие без его исполнения и даёт тесту задержать возврат рабочего потока.
  bool post(std::function<void()> callback) noexcept override
  {
    try
    {
      std::unique_lock lock(mutex_);
      queue_.push_back(std::move(callback));
      if (!firstPosted_)
      {
        firstPosted_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this]() { return released_; });
      }
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  /// Ожидает, пока первое итоговое событие будет доступно владельцу контроллера.
  bool waitForFirstPost()
  {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, std::chrono::seconds(3), [this]() { return firstPosted_; });
  }

  /// Выполняет накопленное событие до выхода старого рабочего потока.
  void drain()
  {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard lock(mutex_);
      pending.swap(queue_);
    }
    for (auto & callback : pending)
      callback();
  }

  /// Разрешает рабочему потоку завершить постановку события.
  void release()
  {
    std::lock_guard lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  std::deque<std::function<void()>> queue_;
  bool firstPosted_ = false;
  bool released_ = false;
};

/// Отклоняет все события и позволяет проверить невыбрасывающую границу доставки.
class RejectingDispatcher final : public IApplicationDispatcher
{
public:
  /// Учитывает попытку и возвращает отказ без выполнения функции.
  bool post(std::function<void()>) noexcept override
  {
    ++attempts;
    return false;
  }

  std::atomic<std::size_t> attempts = 0;
};

/// Отклоняет заданное число первых событий, а остальные накапливает в очереди.
class SelectiveDispatcher final : public IApplicationDispatcher
{
public:
  /// Отклоняет событие по оставшемуся счётчику либо сохраняет его для явной доставки.
  bool post(std::function<void()> callback) noexcept override
  {
    try
    {
      std::lock_guard lock(mutex_);
      if (rejectCount_ > 0)
      {
        --rejectCount_;
        return false;
      }
      queue_.push_back(std::move(callback));
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  /// Выполняет все принятые события.
  std::size_t drain()
  {
    std::deque<std::function<void()>> pending;
    {
      std::lock_guard lock(mutex_);
      pending.swap(queue_);
    }
    for (auto & callback : pending)
      callback();
    return pending.size();
  }

private:
  std::mutex mutex_;
  std::size_t rejectCount_ = 1;
  std::deque<std::function<void()>> queue_;
};

/// Учитывает освобождение результатов без доступа к файловой системе.
class TrackingDocumentGateway final : public IPolygonDocumentGateway
{
public:
  /// Не используется в тестах фоновой доставки.
  PolygonDocumentLoadResult load(const std::string &) override { return {}; }
  /// Не используется в тестах фоновой доставки.
  PolygonDocumentOperationResult save(const std::string &, PolygonSolutionHandle) override { return {}; }
  /// Учитывает освобождение документа.
  void release(PolygonDocumentHandle) noexcept override { ++releasedDocuments; }
  /// Учитывает освобождение решения и запоминает последний идентификатор.
  void release(PolygonSolutionHandle solution) noexcept override
  {
    lastReleasedSolution = solution.value;
    ++releasedSolutions;
  }

  std::atomic<std::size_t> releasedDocuments = 0;
  std::atomic<std::size_t> releasedSolutions = 0;
  std::atomic<std::uint64_t> lastReleasedSolution = 0;
};

/// Немедленно возвращает результат или заданную ошибку для проверки жизненного цикла потока.
class ImmediateBackend final : public IPolygonNestingBackend
{
public:
  bool fail = false;
  bool reportProgress = false;
  NestingRunResult result;

  /// Возвращает пустой результат либо имитирует исключение внутренней реализации.
  NestingRunResult run(PolygonDocumentHandle, const NestingRunRequest &, const Control & control) override
  {
    if (fail)
      throw std::runtime_error("искусственная ошибка решателя");
    if (reportProgress && control.progress)
      control.progress({NestingProgressStage::Instances, 1, 2, 0});
    return result;
  }
};

/// Удерживает рабочий поток до запроса отмены при разрушении средства запуска.
class BlockingBackend final : public IPolygonNestingBackend
{
public:
  std::atomic<bool> entered = false;
  std::atomic<bool> finished = false;

  /// Ожидает отмены, не создавая сохраняемого результата.
  NestingRunResult run(PolygonDocumentHandle, const NestingRunRequest &, const Control & control) override
  {
    entered = true;
    while (!control.cancellationRequested())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    finished = true;
    NestingRunResult result;
    result.completion = NestingCompletion::Cancelled;
    return result;
  }
};

/// Возвращает подготовленную модель и запоминает освобождение без ONNX Runtime.
class ImmediateModelGateway final : public IPolygonModelGateway
{
public:
  /// Возвращает успешный результат либо имитирует исключение.
  PolygonModelLoadResult load(const std::string &) override
  {
    entered = true;
    if (fail)
      throw std::runtime_error("искусственная ошибка модели");
    return {true, {}, {7}, "policy", "hash"};
  }
  /// Потокобезопасно запоминает освобождённую модель.
  void release(PolygonModelHandle model) noexcept override
  {
    releasedValue = model.value;
    ++releaseCount;
  }

  std::atomic<bool> entered = false;
  bool fail = false;
  std::atomic<std::uint64_t> releasedValue = 0;
  std::atomic<std::size_t> releaseCount = 0;
};

/// Задерживает успешную загрузку модели до управляемого запроса отмены.
class BlockingModelGateway final : public IPolygonModelGateway
{
public:
  /// Сообщает о входе, ожидает разрешения теста и возвращает зарегистрированную модель.
  PolygonModelLoadResult load(const std::string &) override
  {
    entered = true;
    while (!allowReturn.load())
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return {true, {}, {9}, "policy", "hash"};
  }
  /// Учитывает освобождение модели после обнаружения отмены.
  void release(PolygonModelHandle model) noexcept override
  {
    releasedValue = model.value;
    ++releaseCount;
  }

  std::atomic<bool> entered = false;
  std::atomic<bool> allowReturn = false;
  std::atomic<std::uint64_t> releasedValue = 0;
  std::atomic<std::size_t> releaseCount = 0;
};

/// Ожидает выполнения условия с коротким предельным сроком.
bool waitUntil(const std::function<bool()> & predicate)
{
  for (int attempt = 0; attempt < 3000; ++attempt)
  {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}
} // namespace

#ifdef AIPACKAGING_DESKTOP_BACKEND_TESTS
/// Проверяет строгую загрузку, выполнение, точную регистрацию и сохранение результата.
TEST(PolygonDesktopInfrastructure, LoadsRunsValidatesAndSaves)
{
  const auto input = writeProblem();
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-a6-solution.json";
  const auto store = std::make_shared<PolygonArtifactStore>();
  LocalPolygonDocumentGateway gateway(store);
  BaselinePolygonBackend backend(store);
  const PolygonDocumentLoadResult loaded = gateway.load(input.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  ASSERT_EQ(loaded.unplacedInstances.size(), 2);

  for (const BaselineAlgorithm algorithm :
       {BaselineAlgorithm::InputFirstFit, BaselineAlgorithm::AreaLeftBottom, BaselineAlgorithm::MaxSideLeftBottom,
        BaselineAlgorithm::RandomLeftBottom, BaselineAlgorithm::Beam})
  {
    NestingRunRequest request;
    request.algorithm = algorithm;
    request.timeoutMs = 0;
    request.randomIterations = 2;
    request.beamWidth = 2;
    request.maxExpandedStates = 100;
    NestingRunResult result = backend.run(loaded.document, request, {});
    ASSERT_TRUE(result.solution.has_value());
    EXPECT_EQ(result.provenance, NestingProvenance::Baseline);
    EXPECT_FALSE(result.implementationName.empty());
    const PolygonDocumentOperationResult saved = gateway.save(output.string(), *result.solution);
    ASSERT_TRUE(saved.success) << saved.error;
    const PolygonSolutionLoadResult parsed = loadPolygonSolutionFromFile(output.string());
    ASSERT_TRUE(parsed.success) << parsed.error;
    EXPECT_TRUE(validatePolygonSolution(testProblem(), parsed.solution).success);
    gateway.release(*result.solution);
  }
  gateway.release(loaded.document);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}
#endif

#ifdef AIPACKAGING_DESKTOP_GATEWAY_TESTS
/// Проверяет, что повреждённое решение не получает сохраняемый идентификатор.
TEST(PolygonDesktopInfrastructure, RejectsCorruptSolution)
{
  const auto store = std::make_shared<PolygonArtifactStore>();
  std::string error;
  std::unique_ptr<PolygonEnvironment> created = PolygonEnvironment::Create(testProblem(), error);
  ASSERT_NE(created, nullptr) << error;
  std::shared_ptr<PolygonEnvironment> environment(std::move(created));
  const PolygonDocumentHandle document = store->addDocument(testProblem(), environment);
  SolverConfig config;
  config.timeoutMs = 0;
  PolygonSolution corrupt = solvePolygonProblem(testProblem(), config);
  ++corrupt.objective.usedLength;
  EXPECT_FALSE(store->addValidatedSolution(document, std::move(corrupt), error).has_value());
  EXPECT_FALSE(error.empty());
}

/// Проверяет загрузку и сохранение через шлюз без участия внутренней реализации раскроя.
TEST(PolygonDesktopInfrastructure, LoadsAndSavesValidatedSolutionThroughGateway)
{
  const auto input = writeProblem();
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-gateway-solution.json";
  const auto store = std::make_shared<PolygonArtifactStore>();
  LocalPolygonDocumentGateway gateway(store);
  const PolygonDocumentLoadResult loaded = gateway.load(input.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  SolverConfig config;
  config.timeoutMs = 0;
  std::string error;
  const auto solution = store->addValidatedSolution(loaded.document, solvePolygonProblem(testProblem(), config), error);
  ASSERT_TRUE(solution.has_value()) << error;
  const PolygonDocumentOperationResult saved = gateway.save(output.string(), *solution);
  ASSERT_TRUE(saved.success) << saved.error;
  EXPECT_TRUE(loadPolygonSolutionFromFile(output.string()).success);
  gateway.release(*solution);
  gateway.release(loaded.document);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}
#endif

#ifdef AIPACKAGING_DESKTOP_JOB_TESTS
/// Проверяет асинхронную доставку результата только через очередь диспетчера.
TEST(PolygonDesktopInfrastructure, DeliversCompletionThroughDispatcherQueue)
{
  const auto input = writeProblem();
  const auto store = std::make_shared<PolygonArtifactStore>();
  auto gateway = std::make_shared<LocalPolygonDocumentGateway>(store);
  const PolygonDocumentLoadResult loaded = gateway->load(input.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  auto backend = std::make_shared<BaselinePolygonBackend>(store);
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadNestingJobRunner runner(backend, dispatcher, gateway);
  std::atomic<bool> delivered = false;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&delivered](NestingJobHandle, const NestingRunResult &)
  {
    delivered = true;
    return true;
  };
  std::string error;
  NestingRunRequest request;
  request.timeoutMs = 0;
  ASSERT_TRUE(runner.start(loaded.document, request, std::move(callbacks), error).has_value()) << error;
  EXPECT_FALSE(delivered.load());
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_TRUE(delivered.load());
  std::filesystem::remove(input);
}

/// Разрешает повторный запуск из итогового события, пока предыдущий поток ещё выходит.
TEST(PolygonDesktopInfrastructure, AllowsRestartFromEarlyCompletion)
{
  auto backend = std::make_shared<ImmediateBackend>();
  auto dispatcher = std::make_shared<PausingDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  std::future<std::optional<NestingJobHandle>> restarted;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&](NestingJobHandle, const NestingRunResult &)
  {
    restarted = std::async(std::launch::async,
                           [&runner]()
                           {
                             std::string error;
                             return runner.start({1}, {}, {}, error);
                           });
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(dispatcher->waitForFirstPost());
  dispatcher->drain();
  dispatcher->release();
  ASSERT_TRUE(restarted.valid());
  ASSERT_EQ(restarted.wait_for(std::chrono::seconds(3)), std::future_status::ready);
  EXPECT_TRUE(restarted.get().has_value());
}

/// Освобождает непереданное решение и разрешает следующий запуск после отказа диспетчера.
TEST(PolygonDesktopInfrastructure, ReleasesSolutionAfterRejectedCompletion)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->result.solution = PolygonSolutionHandle{91};
  auto dispatcher = std::make_shared<RejectingDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  std::atomic<bool> called = false;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&called](NestingJobHandle, const NestingRunResult &)
  {
    called = true;
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return documents->releasedSolutions.load() == 1; }));
  EXPECT_FALSE(called.load());
  EXPECT_EQ(documents->lastReleasedSolution.load(), 91U);

  ASSERT_TRUE(runner.start({1}, {}, {}, error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return documents->releasedSolutions.load() == 2; }));
}

/// Освобождает итоговое решение, если функция завершения не была задана.
TEST(PolygonDesktopInfrastructure, ReleasesSolutionWithoutCompletionCallback)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->result.solution = PolygonSolutionHandle{92};
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, {}, error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return documents->releasedSolutions.load() == 1; }));
  EXPECT_EQ(dispatcher->size(), 0U);
}

/// Удерживает решение принятого события до выполнения либо уничтожения очереди.
TEST(PolygonDesktopInfrastructure, ReleasesSolutionWhenQueuedCompletionIsDiscarded)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->result.solution = PolygonSolutionHandle{93};
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  {
    StdThreadNestingJobRunner runner(backend, dispatcher, documents);
    NestingJobCallbacks callbacks;
    callbacks.completed = [](NestingJobHandle, const NestingRunResult &)
    {
      return true;
    };
    std::string error;
    ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
    ASSERT_TRUE(waitUntil([&]() { return dispatcher->size() == 1; }));
    EXPECT_EQ(documents->releasedSolutions.load(), 0U);
  }
  EXPECT_EQ(documents->releasedSolutions.load(), 0U);
  EXPECT_EQ(dispatcher->discard(), 1U);
  EXPECT_EQ(documents->releasedSolutions.load(), 1U);
}

/// Перехватывает исключение итоговой функции и освобождает не принятое ею решение.
TEST(PolygonDesktopInfrastructure, ReleasesSolutionAfterCompletionCallbackException)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->result.solution = PolygonSolutionHandle{94};
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  NestingJobCallbacks callbacks;
  callbacks.completed = [](NestingJobHandle, const NestingRunResult &) -> bool
  {
    throw std::runtime_error("искусственная ошибка обработчика");
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_EQ(documents->releasedSolutions.load(), 1U);
}

/// Освобождает решение, если итоговая функция явно не приняла владение.
TEST(PolygonDesktopInfrastructure, ReleasesSolutionAfterCompletionRejection)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->result.solution = PolygonSolutionHandle{95};
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  NestingJobCallbacks callbacks;
  callbacks.completed = [](NestingJobHandle, const NestingRunResult &)
  {
    return false;
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_EQ(documents->releasedSolutions.load(), 1U);
}

/// Пропускает отклонённое уведомление о ходе и доставляет последующий итог.
TEST(PolygonDesktopInfrastructure, ContinuesAfterRejectedProgress)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->reportProgress = true;
  auto dispatcher = std::make_shared<SelectiveDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  std::atomic<bool> progressCalled = false;
  std::atomic<bool> completed = false;
  NestingJobCallbacks callbacks;
  callbacks.progress = [&progressCalled](NestingJobHandle, const NestingProgress &)
  {
    progressCalled = true;
  };
  callbacks.completed = [&completed](NestingJobHandle, const NestingRunResult &)
  {
    completed = true;
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil(
    [&]()
    {
      dispatcher->drain();
      return completed.load();
    }));
  EXPECT_FALSE(progressCalled.load());
}

/// Превращает исключение решателя в событие ошибки и освобождает запуск.
TEST(PolygonDesktopInfrastructure, ReleasesJobOnBackendException)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->fail = true;
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  StdThreadNestingJobRunner runner(backend, dispatcher, documents);
  std::string failure;
  NestingJobCallbacks callbacks;
  callbacks.failed = [&](NestingJobHandle, const std::string & message)
  {
    failure = message;
  };
  std::string error;
  ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_EQ(failure, "искусственная ошибка решателя");
  backend->fail = false;
  EXPECT_TRUE(runner.start({1}, {}, {}, error).has_value()) << error;
}

/// Проверяет согласованную отмену и отсутствие сохраняемого решения.
TEST(PolygonDesktopInfrastructure, CancellationReturnsUnsavablePartial)
{
  const auto input = writeProblem();
  const auto store = std::make_shared<PolygonArtifactStore>();
  auto gateway = std::make_shared<LocalPolygonDocumentGateway>(store);
  const PolygonDocumentLoadResult loaded = gateway->load(input.string());
  auto backend = std::make_shared<BaselinePolygonBackend>(store);
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadNestingJobRunner runner(backend, dispatcher, gateway);
  std::optional<NestingRunResult> result;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&result](NestingJobHandle, NestingRunResult value)
  {
    result = std::move(value);
    return true;
  };
  std::string error;
  NestingRunRequest request;
  request.algorithm = BaselineAlgorithm::Beam;
  request.maxExpandedStates = 1'000'000;
  const auto job = runner.start(loaded.document, request, std::move(callbacks), error);
  ASSERT_TRUE(job.has_value()) << error;
  runner.cancel(*job);
  ASSERT_TRUE(waitUntil(
    [&]()
    {
      dispatcher->drain();
      return result.has_value();
    }));
  const NestingRunResult completed = result.value_or(NestingRunResult{});
  EXPECT_EQ(completed.completion, NestingCompletion::Cancelled);
  EXPECT_FALSE(completed.solution.has_value());
  std::filesystem::remove(input);
}

/// Проверяет, что закрытие рабочего контура запрашивает отмену и присоединяет поток.
TEST(PolygonDesktopInfrastructure, DestructionStopsAndJoinsWorker)
{
  auto backend = std::make_shared<BlockingBackend>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  auto documents = std::make_shared<TrackingDocumentGateway>();
  {
    StdThreadNestingJobRunner runner(backend, dispatcher, documents);
    NestingJobCallbacks callbacks;
    std::string error;
    ASSERT_TRUE(runner.start({1}, {}, std::move(callbacks), error).has_value()) << error;
    ASSERT_TRUE(waitUntil([&]() { return backend->entered.load(); }));
  }
  EXPECT_TRUE(backend->finished.load());
}

/// Проверяет отложенную доставку результата фоновой проверки модели.
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
TEST(PolygonDesktopInfrastructure, LoadsModelThroughBackgroundRunner)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  bool completed = false;
  PolygonModelJobCallbacks callbacks;
  callbacks.completed = [&completed](PolygonModelJobHandle, const PolygonModelLoadResult & result)
  {
    completed = result.success && result.model.value == 7;
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return gateway->entered.load(); }));
  EXPECT_FALSE(completed);
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_TRUE(completed);
  runner.release({7});
  EXPECT_EQ(gateway->releaseCount.load(), 1U);
  EXPECT_EQ(gateway->releasedValue.load(), 7U);
}

/// Освобождает модель после отказа диспетчера принять итоговое событие.
TEST(PolygonDesktopInfrastructure, ReleasesModelAfterRejectedCompletion)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<RejectingDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  std::atomic<bool> called = false;
  PolygonModelJobCallbacks callbacks;
  callbacks.completed = [&called](PolygonModelJobHandle, const PolygonModelLoadResult &)
  {
    called = true;
    return true;
  };
  std::string error;
  ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return gateway->releaseCount.load() == 1; }));
  EXPECT_FALSE(called.load());
  EXPECT_EQ(gateway->releasedValue.load(), 7U);
  EXPECT_TRUE(runner.start("model", {}, error).has_value()) << error;
  EXPECT_TRUE(waitUntil([&]() { return gateway->releaseCount.load() == 2; }));
}

/// Освобождает модель, если итоговая функция не задана.
TEST(PolygonDesktopInfrastructure, ReleasesModelWithoutCompletionCallback)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  std::string error;
  ASSERT_TRUE(runner.start("model", {}, error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return gateway->releaseCount.load() == 1; }));
  EXPECT_EQ(dispatcher->size(), 0U);
}

/// Освобождает модель принятого события после уничтожения очереди без выполнения.
TEST(PolygonDesktopInfrastructure, ReleasesModelWhenQueuedCompletionIsDiscarded)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  {
    StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
    PolygonModelJobCallbacks callbacks;
    callbacks.completed = [](PolygonModelJobHandle, const PolygonModelLoadResult &)
    {
      return true;
    };
    std::string error;
    ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
    ASSERT_TRUE(waitUntil([&]() { return dispatcher->size() == 1; }));
    EXPECT_EQ(gateway->releaseCount.load(), 0U);
  }
  EXPECT_EQ(gateway->releaseCount.load(), 0U);
  EXPECT_EQ(dispatcher->discard(), 1U);
  EXPECT_EQ(gateway->releaseCount.load(), 1U);
  EXPECT_EQ(gateway->releasedValue.load(), 7U);
}

/// Перехватывает исключение итоговой функции и освобождает модель.
TEST(PolygonDesktopInfrastructure, ReleasesModelAfterCompletionCallbackException)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  PolygonModelJobCallbacks callbacks;
  callbacks.completed = [](PolygonModelJobHandle, const PolygonModelLoadResult &) -> bool
  {
    throw std::runtime_error("искусственная ошибка обработчика модели");
  };
  std::string error;
  ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_EQ(gateway->releaseCount.load(), 1U);
}

/// Освобождает модель, если итоговая функция явно не приняла владение.
TEST(PolygonDesktopInfrastructure, ReleasesModelAfterCompletionRejection)
{
  auto gateway = std::make_shared<ImmediateModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  PolygonModelJobCallbacks callbacks;
  callbacks.completed = [](PolygonModelJobHandle, const PolygonModelLoadResult &)
  {
    return false;
  };
  std::string error;
  ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_EQ(gateway->releaseCount.load(), 1U);
}

/// Освобождает успешно созданную модель, если отмена запрошена до завершения загрузки.
TEST(PolygonDesktopInfrastructure, ReleasesModelAfterCancellation)
{
  auto gateway = std::make_shared<BlockingModelGateway>();
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadPolygonModelJobRunner runner(gateway, dispatcher);
  std::atomic<bool> cancelled = false;
  PolygonModelJobCallbacks callbacks;
  callbacks.cancelled = [&cancelled](PolygonModelJobHandle)
  {
    cancelled = true;
  };
  std::string error;
  const auto job = runner.start("model", std::move(callbacks), error);
  ASSERT_TRUE(job.has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return gateway->entered.load(); }));
  runner.cancel(*job);
  gateway->allowReturn = true;
  ASSERT_TRUE(waitUntil([&]() { return gateway->releaseCount.load() == 1; }));
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_TRUE(cancelled.load());
  EXPECT_EQ(gateway->releasedValue.load(), 9U);
}
#endif
#endif

#if defined(AIPACKAGING_DESKTOP_BACKEND_TESTS) && defined(AIPACKAGING_HAS_ONNX_BACKEND)
/// Проверяет загрузку комплекта и нейросетевой запуск через прикладные идентификаторы.
TEST(PolygonDesktopInfrastructure, LoadsModelAndRunsNeuralBackend)
{
  const auto input = writeProblem();
  const auto store = std::make_shared<PolygonArtifactStore>();
  LocalPolygonDocumentGateway documents(store);
  LocalPolygonModelGateway models(store);
  const PolygonDocumentLoadResult loaded = documents.load(input.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  const PolygonModelLoadResult model = models.load(AIPACKAGING_ONNX_TEST_MODEL);
  ASSERT_TRUE(model.success) << model.error;

  OnnxPolygonBackend backend(store);
  NestingRunRequest request;
  request.method = NestingMethod::Neural;
  request.neuralSelection = NeuralSelectionMode::Greedy;
  request.model = model.model;
  request.timeoutMs = 0;
  const NestingRunResult result = backend.run(loaded.document, request, {});
  EXPECT_EQ(result.provenance, NestingProvenance::Neural);
  EXPECT_TRUE(result.solution.has_value());
  EXPECT_FALSE(result.partial);

  models.release(model.model);
  documents.release(loaded.document);
  std::filesystem::remove(input);
}

/// Проверяет гибридный запуск с поставляемой моделью и сохранение проверенного результата.
TEST(PolygonDesktopInfrastructure, RunsRecommendedHybridAndSaves)
{
  const auto input = writeProblem();
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-u1-hybrid-solution.json";
  const auto store = std::make_shared<PolygonArtifactStore>();
  LocalPolygonDocumentGateway documents(store);
  LocalPolygonModelGateway models(store);
  const PolygonDocumentLoadResult loaded = documents.load(input.string());
  const PolygonModelLoadResult model = models.load(AIPACKAGING_ONNX_TEST_MODEL);
  ASSERT_TRUE(loaded.success) << loaded.error;
  ASSERT_TRUE(model.success) << model.error;

  OnnxPolygonBackend backend(store);
  NestingRunRequest request;
  request.method = NestingMethod::Hybrid;
  request.model = model.model;
  request.neuralRollouts = 16;
  request.fallbackRandomIterations = 64;
  request.timeoutMs = 0;
  const NestingRunResult result = backend.run(loaded.document, request, {});
  EXPECT_EQ(result.provenance, NestingProvenance::Hybrid);
  ASSERT_TRUE(result.solution.has_value());
  const PolygonDocumentOperationResult saved = documents.save(output.string(), *result.solution);
  ASSERT_TRUE(saved.success) << saved.error;
  const PolygonSolutionLoadResult parsed = loadPolygonSolutionFromFile(output.string());
  ASSERT_TRUE(parsed.success) << parsed.error;
  EXPECT_TRUE(validatePolygonSolution(testProblem(), parsed.solution).success);

  documents.release(*result.solution);
  models.release(model.model);
  documents.release(loaded.document);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}
#endif
