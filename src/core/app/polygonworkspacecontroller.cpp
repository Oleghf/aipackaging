#include <chrono>
#include <exception>
#include <set>
#include <utility>

#include <polygonio.h>
#include <polygonsolver.h>
#include <polygonworkspacecontroller.h>

namespace
{
using namespace aipackaging::solver;

/// Выполняет обычный baseline через общий управляемый API solver-а.
class BaselinePolygonSolverBackend final : public IPolygonSolverBackend
{
public:
  /// Передаёт задачу, настройки и execution-control полигональному solver-у.
  PolygonSolverExecutionResult run(const PolygonProblem & problem, const SolverConfig & config,
                                   const PolygonExecutionControl & control) override
  {
    return runPolygonProblem(problem, config, control);
  }
};

/// Переводит микронную точку кольца в миллиметровую presentation-точку.
PolygonViewPoint toViewPoint(const PolygonPoint64 & point, std::int64_t offsetX, std::int64_t offsetY)
{
  return {static_cast<double>(point.x + offsetX) / 1000.0, static_cast<double>(point.y + offsetY) / 1000.0};
}

/// Проверяет положительные детерминированные бюджеты GUI-запуска.
bool validConfig(const SolverConfig & config)
{
  return config.randomIterations > 0 && config.beamWidth > 0 && config.maxExpandedStates > 0;
}
} // namespace

/// Сохраняет зависимости и публикует исходное пустое состояние.
PolygonWorkspaceController::PolygonWorkspaceController(std::shared_ptr<IPolygonWorkspaceView> view,
                                                       std::shared_ptr<IPolygonSolverBackend> backend)
  : view_(std::move(view))
  , backend_(backend ? std::move(backend) : std::make_shared<BaselinePolygonSolverBackend>())
{
  snapshot_.statusText = "Откройте polygon_problem v1";
  publish();
}

/// Запрашивает cooperative cancellation и синхронно завершает принадлежащий worker.
PolygonWorkspaceController::~PolygonWorkspaceController()
{
  if (worker_.joinable())
  {
    worker_.request_stop();
    worker_.join();
  }
}

/// Устанавливает callbacks, удерживающие контроллер только на время конкретного вызова.
void PolygonWorkspaceController::bindActions()
{
  const std::weak_ptr<PolygonWorkspaceController> weak = weak_from_this();
  PolygonWorkspaceActions actions;
  actions.openProblem = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      self->openProblem(path);
  };
  actions.saveSolution = [weak](const std::string & path)
  {
    if (const auto self = weak.lock())
      self->saveSolution(path);
  };
  actions.start = [weak](const SolverConfig & config)
  {
    if (const auto self = weak.lock())
      self->start(config);
  };
  actions.cancel = [weak]()
  {
    if (const auto self = weak.lock())
      self->cancel();
  };
  view_->setPolygonWorkspaceActions(std::move(actions));
}

/// Загружает и повторно нормализует задачу, заменяя рабочую сцену только после успеха.
void PolygonWorkspaceController::openProblem(const std::string & filePath)
{
  if (filePath.empty() || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  const PolygonProblemLoadResult loaded = loadPolygonProblemFromFile(filePath);
  if (!loaded.success)
  {
    if (!problem_)
      snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Ошибка загрузки: " + loaded.error;
    publish();
    return;
  }
  std::string error;
  std::unique_ptr<PolygonEnvironment> environment = PolygonEnvironment::Create(loaded.problem, error);
  if (!environment)
  {
    if (!problem_)
      snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Ошибка геометрии: " + error;
    publish();
    return;
  }

  problem_ = loaded.problem;
  environment_ = std::move(environment);
  solution_.reset();
  saveable_ = false;
  snapshot_ = {};
  snapshot_.state = PolygonWorkspaceState::Ready;
  snapshot_.problemId = problem_->problemId;
  snapshot_.statusText = "Задача загружена";
  rebuildPresentation(nullptr);
  publish();
}

/// Проверяет доступность результата и делегирует запись strict serializer-у.
void PolygonWorkspaceController::saveSolution(const std::string & filePath)
{
  if (filePath.empty() || !saveable_ || !solution_ || !problem_)
    return;
  const ValidationResult validation = validatePolygonSolution(*problem_, *solution_);
  if (!validation.success)
  {
    saveable_ = false;
    snapshot_.statusText = "Сохранение запрещено: " + validation.error;
    publish();
    return;
  }
  std::string error;
  snapshot_.statusText =
    savePolygonSolutionToFile(filePath, *solution_, error) ? "Решение сохранено" : "Ошибка сохранения: " + error;
  publish();
}

/// Копирует неизменяемый вход в worker и доставляет progress/result через UI-dispatcher.
void PolygonWorkspaceController::start(const SolverConfig & config)
{
  if (!problem_ || snapshot_.state == PolygonWorkspaceState::Running)
    return;
  if (!validConfig(config))
  {
    snapshot_.statusText = "Параметры solver должны быть положительными";
    publish();
    return;
  }
  if (worker_.joinable())
    worker_.join();

  const PolygonProblem problem = *problem_;
  const std::shared_ptr<IPolygonSolverBackend> backend = backend_;
  const std::weak_ptr<PolygonWorkspaceController> weak = weak_from_this();
  const std::uint64_t currentRun = ++runId_;
  snapshot_.state = PolygonWorkspaceState::Running;
  snapshot_.statusText = "Выполняется полигональный поиск";
  snapshot_.solverName = toString(config.solver);
  snapshot_.progress = {};
  publish();

  worker_ = std::jthread(
    [weak, backend, problem, config, currentRun](const std::stop_token & stopToken)
    {
      auto lastProgress = std::chrono::steady_clock::time_point::min();
      PolygonExecutionControl control;
      control.cancellationRequested = [stopToken]()
      {
        return stopToken.stop_requested();
      };
      control.progress = [weak, currentRun, &lastProgress](const PolygonSolverProgress & progress)
      {
        const auto now = std::chrono::steady_clock::now();
        const bool finalUpdate = progress.total > 0 && progress.completed >= progress.total;
        if (!finalUpdate && lastProgress != std::chrono::steady_clock::time_point::min() &&
            now - lastProgress < std::chrono::milliseconds(100))
          return;
        lastProgress = now;
        if (const auto self = weak.lock())
        {
          const auto view = self->view_;
          view->postToPolygonUi(
            [weak, currentRun, progress]()
            {
              if (const auto target = weak.lock())
                target->acceptProgress(currentRun, progress);
            });
        }
      };
      try
      {
        PolygonSolverExecutionResult result = backend->run(problem, config, control);
        if (const auto self = weak.lock())
        {
          const auto view = self->view_;
          view->postToPolygonUi(
            [weak, currentRun, result = std::move(result)]() mutable
            {
              if (const auto target = weak.lock())
                target->acceptResult(currentRun, std::move(result));
            });
        }
      }
      catch (const std::exception & error)
      {
        if (const auto self = weak.lock())
        {
          const auto view = self->view_;
          const std::string message = error.what();
          view->postToPolygonUi(
            [weak, currentRun, message]()
            {
              if (const auto target = weak.lock())
                target->acceptFailure(currentRun, message);
            });
        }
      }
    });
}

/// Передаёт stop-request worker-у и оставляет окончательное состояние completion callback-у.
void PolygonWorkspaceController::cancel()
{
  if (snapshot_.state != PolygonWorkspaceState::Running || !worker_.joinable())
    return;
  worker_.request_stop();
  snapshot_.statusText = "Запрошена отмена…";
  publish();
}

/// Возвращает копию последнего UI-снимка.
PolygonWorkspaceSnapshot PolygonWorkspaceController::snapshot() const
{
  return snapshot_;
}

/// Вычисляет доступность кнопок из единственного состояния и отправляет снимок view.
void PolygonWorkspaceController::publish()
{
  const bool running = snapshot_.state == PolygonWorkspaceState::Running;
  snapshot_.canOpen = !running;
  snapshot_.canRun = problem_.has_value() && !running;
  snapshot_.canCancel = running;
  snapshot_.canSave = saveable_ && !running;
  if (view_)
    view_->presentPolygonWorkspace(snapshot_);
}

/// Игнорирует устаревшие события и обновляет только диагностические счётчики.
void PolygonWorkspaceController::acceptProgress(std::uint64_t runId, const PolygonSolverProgress & progress)
{
  if (runId != runId_ || snapshot_.state != PolygonWorkspaceState::Running)
    return;
  snapshot_.progress = progress;
  publish();
}

/// Повторно валидирует результат и атомарно заменяет presentation-модель актуального запуска.
void PolygonWorkspaceController::acceptResult(std::uint64_t runId, PolygonSolverExecutionResult result)
{
  if (runId != runId_ || snapshot_.state != PolygonWorkspaceState::Running || !problem_)
    return;
  const ValidationResult validation = validatePolygonSolution(*problem_, result.solution);
  if (!validation.success)
  {
    snapshot_.state = PolygonWorkspaceState::Error;
    snapshot_.statusText = "Solver вернул невалидный результат: " + validation.error;
    publish();
    return;
  }

  const bool complete = result.solution.complete();
  const SolveStatus solutionStatus = result.solution.status;
  solution_ = std::move(result.solution);
  rebuildPresentation(&*solution_);
  if (result.cancelled)
  {
    snapshot_.state = PolygonWorkspaceState::Cancelled;
    snapshot_.statusText = "Поиск отменён; показан последний partial";
    saveable_ = false;
  }
  else
  {
    snapshot_.state = PolygonWorkspaceState::Completed;
    switch (solutionStatus)
    {
      case SolveStatus::TimedOut:
        snapshot_.statusText = "Истёк timeout; показано лучшее проверенное partial";
        break;
      case SolveStatus::BudgetExhausted:
        snapshot_.statusText = "Исчерпан бюджет; показано лучшее проверенное решение";
        break;
      case SolveStatus::UnsupportedEnvironment:
        snapshot_.statusText = "Превышен предел среды; показано лучшее проверенное partial";
        break;
      case SolveStatus::NoSolutionFound:
        snapshot_.statusText = "Полное решение не найдено; показан проверенный partial";
        break;
      default:
        snapshot_.statusText = complete ? "Полная раскладка построена" : "Показано лучшее частичное решение";
        break;
    }
    saveable_ = true;
  }
  publish();
}

/// Сохраняет прежнюю сцену и переводит только актуальный запуск в состояние error.
void PolygonWorkspaceController::acceptFailure(std::uint64_t runId, const std::string & error)
{
  if (runId != runId_ || snapshot_.state != PolygonWorkspaceState::Running)
    return;
  snapshot_.state = PolygonWorkspaceState::Error;
  snapshot_.statusText = "Ошибка solver backend: " + error;
  publish();
}

/// Переводит авторитетные микронные кольца и objective в миллиметровый view snapshot.
void PolygonWorkspaceController::rebuildPresentation(const PolygonSolution * solution)
{
  snapshot_.scene = {};
  snapshot_.unplacedInstances.clear();
  snapshot_.objective = {};
  snapshot_.metrics = {};
  snapshot_.solutionStatus.clear();
  snapshot_.partial = false;
  if (!problem_ || !environment_)
    return;

  snapshot_.problemId = problem_->problemId;
  snapshot_.scene.sheetWidth = problem_->sheet.width;
  snapshot_.scene.sheetHeight = problem_->sheet.height;
  snapshot_.scene.sheetMargin = problem_->manufacturing.sheetMargin;
  std::set<std::pair<std::string, std::uint32_t>> placed;
  if (solution)
  {
    snapshot_.objective = solution->objective;
    snapshot_.metrics = solution->metrics;
    snapshot_.solutionStatus = toString(solution->status);
    snapshot_.solverName = solution->solver.name;
    snapshot_.partial = !solution->complete();
    snapshot_.scene.usedLength = static_cast<double>(solution->objective.usedLength) / 1000.0;
    snapshot_.scene.primaryRemnantWidth = static_cast<double>(solution->objective.primaryRemnantWidth) / 1000.0;
    for (const PolygonPlacement & placement : solution->placements)
    {
      const std::size_t instancePosition = environment_->findInstance(placement.partId, placement.instanceIndex);
      if (instancePosition >= environment_->instances().size())
        continue;
      const std::size_t partIndex = environment_->instances()[instancePosition].partIndex;
      const PolygonOrientation * orientation = environment_->findOrientation(partIndex, placement.rotationDegrees);
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
      snapshot_.scene.placements.push_back(std::move(part));
      placed.emplace(placement.partId, placement.instanceIndex);
    }
  }

  for (const PolygonPartInstance & instance : environment_->instances())
  {
    const std::string & id = problem_->parts[instance.partIndex].id;
    if (!placed.contains({id, instance.instanceIndex}))
      snapshot_.unplacedInstances.push_back(id + " #" + std::to_string(instance.instanceIndex));
  }
}
