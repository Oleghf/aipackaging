#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>
#include <polygonio.h>
#include <polygonworkspacecontroller.h>

namespace
{
using namespace aipackaging::solver;
using namespace std::chrono_literals;

/// Тестовый двойник представления с управляемой очередью обратных вызовов UI.
class PolygonWorkspaceViewStub final : public IPolygonWorkspaceView
{
public:
  /// Сохраняет привязанные контроллером пользовательские действия.
  void setPolygonWorkspaceActions(PolygonWorkspaceActions value) override { actions = std::move(value); }

  /// Сохраняет последний снимок модели представления под взаимной блокировкой.
  void presentPolygonWorkspace(const PolygonWorkspaceSnapshot & value) override
  {
    std::lock_guard lock(mutex_);
    latest_ = value;
  }

  /// Ставит обратный вызов рабочего потока в очередь вместо выполнения в чужом потоке.
  void postToPolygonUi(std::function<void()> callback) override
  {
    std::lock_guard lock(mutex_);
    queue_.push_back(std::move(callback));
  }

  /// Возвращает потокобезопасную копию последнего снимка.
  PolygonWorkspaceSnapshot latest() const
  {
    std::lock_guard lock(mutex_);
    return latest_;
  }

  /// Выполняет поставленные в очередь функции обратного вызова до выполнения условия либо истечения срока.
  bool pumpUntil(const std::function<bool(const PolygonWorkspaceSnapshot &)> & predicate,
                 std::chrono::milliseconds timeout = 3000ms)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
      std::deque<std::function<void()>> pending;
      {
        std::lock_guard lock(mutex_);
        pending.swap(queue_);
      }
      for (auto & callback : pending)
        callback();
      if (predicate(latest()))
        return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  /// Ожидает накопления указанного числа обратных вызовов без их выполнения.
  bool waitForQueued(std::size_t count, std::chrono::milliseconds timeout = 3000ms) const
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
      {
        std::lock_guard lock(mutex_);
        if (queue_.size() >= count)
          return true;
      }
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }

  /// Выполняет новый обратный вызов раньше поставленных сообщений о ходе работы.
  void executeNewest()
  {
    std::function<void()> callback;
    {
      std::lock_guard lock(mutex_);
      callback = std::move(queue_.back());
      queue_.pop_back();
    }
    callback();
  }

  PolygonWorkspaceActions actions;

private:
  mutable std::mutex mutex_;
  PolygonWorkspaceSnapshot latest_;
  std::deque<std::function<void()>> queue_;
};

/// Тестовый двойник реализации, ожидающий остановки и возвращающий частичное решение.
class BlockingPolygonBackend final : public IPolygonSolverBackend
{
public:
  /// Ожидает внешнюю отмену, затем использует рабочий решатель для частичного решения.
  PolygonSolverExecutionResult run(const PolygonProblem & problem, const SolverConfig & config,
                                   const PolygonExecutionControl & control) override
  {
    ++calls;
    while (!control.cancellationRequested || !control.cancellationRequested())
      std::this_thread::sleep_for(1ms);
    PolygonExecutionControl cancelled;
    cancelled.cancellationRequested = []()
    {
      return true;
    };
    return runPolygonProblem(problem, config, cancelled);
  }

  std::atomic<int> calls = 0;
};

/// Тестовый двойник реализации, имитирующий нарушение целевой функции.
class CorruptPolygonBackend final : public IPolygonSolverBackend
{
public:
  /// Возвращает геометрически построенное решение с намеренно подменённой метрикой.
  PolygonSolverExecutionResult run(const PolygonProblem & problem, const SolverConfig & config,
                                   const PolygonExecutionControl &) override
  {
    PolygonSolution solution = solvePolygonProblem(problem, config);
    ++solution.objective.usedLength;
    return {std::move(solution), false};
  }
};

/// Тестовый двойник реализации, возвращающий корректный результат при первом запуске.
class ValidThenCorruptPolygonBackend final : public IPolygonSolverBackend
{
public:
  /// Делает второй и последующие результаты невалидными без изменения их геометрии.
  PolygonSolverExecutionResult run(const PolygonProblem & problem, const SolverConfig & config,
                                   const PolygonExecutionControl &) override
  {
    PolygonSolution solution = solvePolygonProblem(problem, config);
    if (++calls > 1)
      ++solution.objective.usedLength;
    return {std::move(solution), false};
  }

private:
  std::atomic<int> calls = 0;
};

/// Тестовый двойник, возвращающий частичное решение после ограничения времени.
class TimedOutPolygonBackend final : public IPolygonSolverBackend
{
public:
  /// Строит точный пустой снимок и помечает его остановленным по времени.
  PolygonSolverExecutionResult run(const PolygonProblem & problem, const SolverConfig & config,
                                   const PolygonExecutionControl &) override
  {
    PolygonExecutionControl stopped;
    stopped.cancellationRequested = []()
    {
      return true;
    };
    PolygonSolverExecutionResult result = runPolygonProblem(problem, config, stopped);
    result.cancelled = false;
    result.solution.status = SolveStatus::TimedOut;
    return result;
  }
};

/// Создаёт прямоугольный путь для тестов контроллера.
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
PolygonProblem testProblem(double sheetWidth = 100.0, double sheetHeight = 60.0)
{
  PolygonProblem result;
  result.problemId = "desktop-polygon-test";
  result.sheet = {sheetWidth, sheetHeight, "mm"};
  result.manufacturing = {2.0, 1.0, 0.2, 0.05};
  PolygonPart part;
  part.id = "part";
  part.quantity = 2;
  part.outer = rectangle(30.0, 20.0);
  part.allowedRotations = {0, 90};
  result.parts.push_back(std::move(part));
  return result;
}

/// Записывает задачу в изолированный временный файл JSON для файлового сценария.
std::filesystem::path writeProblem(const PolygonProblem & problem, const std::string & suffix)
{
  const std::filesystem::path path = std::filesystem::temp_directory_path() / ("aipackaging-" + suffix + ".json");
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << savePolygonProblemToText(problem);
  return path;
}
} // namespace

/// Проверяет полный сценарий загрузки, асинхронного поиска и сохранения.
TEST(PolygonWorkspaceController, LoadsRunsAndSavesValidatedSolution)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-input");
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-polygon-controller-solution.json";
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view);
  controller->bindActions();
  view->actions.openProblem(input.string());
  ASSERT_EQ(view->latest().state, PolygonWorkspaceState::Ready);

  SolverConfig config;
  config.timeoutMs = 0;
  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Completed; }));
  EXPECT_TRUE(view->latest().canSave);
  EXPECT_FALSE(view->latest().partial);
  EXPECT_EQ(view->latest().scene.placements.size(), 2);

  view->actions.saveSolution(output.string());
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromFile(output.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_TRUE(validatePolygonSolution(testProblem(), loaded.solution).success);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}

/// Проверяет сохранение предыдущей корректной сцены после ошибки нового файла.
TEST(PolygonWorkspaceController, KeepsPreviousSceneAfterLoadError)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-valid");
  const auto invalid = std::filesystem::temp_directory_path() / "aipackaging-polygon-controller-invalid.json";
  {
    std::ofstream output(invalid, std::ios::binary | std::ios::trunc);
    output << "{ invalid";
  }
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view);
  controller->bindActions();
  view->actions.openProblem(input.string());
  const std::string problemId = view->latest().problemId;
  view->actions.openProblem(invalid.string());
  EXPECT_EQ(view->latest().problemId, problemId);
  EXPECT_EQ(view->latest().state, PolygonWorkspaceState::Ready);
  EXPECT_NE(view->latest().statusText.find("Ошибка загрузки"), std::string::npos);
  std::filesystem::remove(input);
  std::filesystem::remove(invalid);
}

/// Проверяет запрет параллельного запуска и несохраняемое частичное решение после отмены.
TEST(PolygonWorkspaceController, CancelsWithoutStartingSecondRun)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-cancel");
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto backend = std::make_shared<BlockingPolygonBackend>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view, backend);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.timeoutMs = 0;
  view->actions.start(config);
  view->actions.start(config);
  while (backend->calls.load() == 0)
    std::this_thread::sleep_for(1ms);
  EXPECT_EQ(backend->calls.load(), 1);
  view->actions.cancel();
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Cancelled; }));
  EXPECT_FALSE(view->latest().canSave);
  EXPECT_TRUE(view->latest().partial);
  std::filesystem::remove(input);
}

/// Проверяет показ и сохранение независимо проверенного частичного решения.
TEST(PolygonWorkspaceController, PublishesAndSavesOrdinaryPartial)
{
  const PolygonProblem problem = testProblem(34.0, 24.0);
  const auto input = writeProblem(problem, "polygon-controller-partial");
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-polygon-controller-partial-solution.json";
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.timeoutMs = 0;
  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Completed; }));
  EXPECT_TRUE(view->latest().partial);
  EXPECT_TRUE(view->latest().canSave);
  EXPECT_EQ(view->latest().objective.placedParts, 1);
  view->actions.saveSolution(output.string());
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromFile(output.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_TRUE(validatePolygonSolution(problem, loaded.solution).success);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}

/// Проверяет, что контроллер не публикует некорректный результат внутренней реализации.
TEST(PolygonWorkspaceController, RejectsCorruptBackendResult)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-corrupt");
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto backend = std::make_shared<CorruptPolygonBackend>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view, backend);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.timeoutMs = 0;
  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Error; }));
  EXPECT_FALSE(view->latest().canSave);
  EXPECT_TRUE(view->latest().scene.placements.empty());
  EXPECT_NE(view->latest().statusText.find("некорректный"), std::string::npos);
  std::filesystem::remove(input);
}

/// Проверяет сохранение последней корректной раскладки при ошибке следующего запуска.
TEST(PolygonWorkspaceController, KeepsValidatedSolutionAfterLaterBackendError)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-retained-result");
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto backend = std::make_shared<ValidThenCorruptPolygonBackend>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view, backend);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.timeoutMs = 0;
  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Completed; }));
  const std::size_t retainedPlacements = view->latest().scene.placements.size();
  ASSERT_TRUE(view->latest().canSave);

  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Error; }));
  EXPECT_EQ(view->latest().scene.placements.size(), retainedPlacements);
  EXPECT_TRUE(view->latest().canSave);
  std::filesystem::remove(input);
}

/// Проверяет частичное решение после ограничения времени отдельно от отмены пользователя.
TEST(PolygonWorkspaceController, PublishesSaveableTimedOutPartial)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-timeout");
  const auto output = std::filesystem::temp_directory_path() / "aipackaging-polygon-controller-timeout-solution.json";
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto backend = std::make_shared<TimedOutPolygonBackend>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view, backend);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.timeoutMs = 1;
  view->actions.start(config);
  ASSERT_TRUE(view->pumpUntil([](const auto & snapshot) { return snapshot.state == PolygonWorkspaceState::Completed; }));
  EXPECT_TRUE(view->latest().partial);
  EXPECT_TRUE(view->latest().canSave);
  view->actions.saveSolution(output.string());
  const PolygonSolutionLoadResult loaded = loadPolygonSolutionFromFile(output.string());
  ASSERT_TRUE(loaded.success) << loaded.error;
  EXPECT_EQ(loaded.solution.status, SolveStatus::TimedOut);
  std::filesystem::remove(input);
  std::filesystem::remove(output);
}

/// Проверяет, что устаревшее сообщение о ходе работы не меняет завершённое состояние.
TEST(PolygonWorkspaceController, IgnoresLateProgressAfterResult)
{
  const auto input = writeProblem(testProblem(), "polygon-controller-late-progress");
  const auto view = std::make_shared<PolygonWorkspaceViewStub>();
  const auto controller = std::make_shared<PolygonWorkspaceController>(view);
  controller->bindActions();
  view->actions.openProblem(input.string());
  SolverConfig config;
  config.solver = SolverKind::InputFirstFit;
  config.timeoutMs = 0;
  view->actions.start(config);

  // Рабочий решатель ставит два сообщения о ходе и итог; исполняем итог первым.
  ASSERT_TRUE(view->waitForQueued(3));
  view->executeNewest();
  ASSERT_EQ(view->latest().state, PolygonWorkspaceState::Completed);
  EXPECT_TRUE(view->latest().canSave);

  view->pumpUntil([](const auto &) { return false; }, 10ms);
  EXPECT_EQ(view->latest().state, PolygonWorkspaceState::Completed);
  EXPECT_TRUE(view->latest().canSave);
  std::filesystem::remove(input);
}
