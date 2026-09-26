#include <stdexcept>
#include <utility>

#include <aipackaging/nesting/polygon_solver.h>
#ifdef AIPACKAGING_HAS_ONNX_BACKEND
#include <aipackaging/inference/polygon_onnx.h>
#endif

#include "polygon_artifact_store_internal.h"
#include "polygon_backends.h"
#include "polygon_presentation_mapper.h"

using namespace aipackaging::solver;
using namespace aipackaging::desktop::detail;

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
  policyControl.baselineProgress = [&control](const SearchProgress & progress)
  {
    if (control.progress)
      control.progress({toProgressStage(progress.stage), progress.completed, progress.total, progress.expandedStates});
  };
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
