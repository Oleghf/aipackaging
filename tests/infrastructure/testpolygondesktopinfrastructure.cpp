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
#include <polygondesktopinfrastructure.h>

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
  /// Добавляет функцию в защищённую очередь.
  void post(std::function<void()> callback) override
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(callback));
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

private:
  std::mutex mutex_;
  std::deque<std::function<void()>> queue_;
};

/// Удерживает первый вызов постановки после помещения события в очередь.
class PausingDispatcher final : public IApplicationDispatcher
{
public:
  /// Ставит событие без его исполнения и даёт тесту задержать возврат рабочего потока.
  void post(std::function<void()> callback) override
  {
    std::unique_lock lock(mutex_);
    queue_.push_back(std::move(callback));
    if (!firstPosted_)
    {
      firstPosted_ = true;
      changed_.notify_all();
      changed_.wait(lock, [this]() { return released_; });
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

/// Немедленно возвращает результат или заданную ошибку для проверки жизненного цикла потока.
class ImmediateBackend final : public IPolygonNestingBackend
{
public:
  bool fail = false;

  /// Возвращает пустой результат либо имитирует исключение внутренней реализации.
  NestingRunResult run(PolygonDocumentHandle, const NestingRunRequest &, const Control &) override
  {
    if (fail)
      throw std::runtime_error("искусственная ошибка решателя");
    return {};
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
  /// Запоминает освобождённую модель.
  void release(PolygonModelHandle model) noexcept override { released = model; }

  std::atomic<bool> entered = false;
  bool fail = false;
  std::optional<PolygonModelHandle> released;
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
  StdThreadNestingJobRunner runner(backend, dispatcher);
  std::atomic<bool> delivered = false;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&delivered](NestingJobHandle, const NestingRunResult &)
  {
    delivered = true;
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
  StdThreadNestingJobRunner runner(backend, dispatcher);
  std::future<std::optional<NestingJobHandle>> restarted;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&](NestingJobHandle, NestingRunResult)
  {
    restarted = std::async(std::launch::async,
                           [&runner]()
                           {
                             std::string error;
                             return runner.start({1}, {}, {}, error);
                           });
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

/// Превращает исключение решателя в событие ошибки и освобождает запуск.
TEST(PolygonDesktopInfrastructure, ReleasesJobOnBackendException)
{
  auto backend = std::make_shared<ImmediateBackend>();
  backend->fail = true;
  auto dispatcher = std::make_shared<QueueDispatcher>();
  StdThreadNestingJobRunner runner(backend, dispatcher);
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
  StdThreadNestingJobRunner runner(backend, dispatcher);
  std::optional<NestingRunResult> result;
  NestingJobCallbacks callbacks;
  callbacks.completed = [&result](NestingJobHandle, NestingRunResult value)
  {
    result = std::move(value);
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
  {
    StdThreadNestingJobRunner runner(backend, dispatcher);
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
  callbacks.completed = [&completed](PolygonModelJobHandle, PolygonModelLoadResult result)
  {
    completed = result.success && result.model.value == 7;
  };
  std::string error;
  ASSERT_TRUE(runner.start("model", std::move(callbacks), error).has_value()) << error;
  ASSERT_TRUE(waitUntil([&]() { return gateway->entered.load(); }));
  EXPECT_FALSE(completed);
  ASSERT_TRUE(waitUntil([&]() { return dispatcher->drain() > 0; }));
  EXPECT_TRUE(completed);
  runner.release({7});
  ASSERT_TRUE(gateway->released.has_value());
  EXPECT_EQ(gateway->released->value, 7U);
}
#endif

#ifdef AIPACKAGING_HAS_ONNX_BACKEND
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
