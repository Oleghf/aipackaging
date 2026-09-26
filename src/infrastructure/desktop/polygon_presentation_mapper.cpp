#include "polygon_presentation_mapper.h"

#include <set>
#include <stdexcept>
#include <utility>

#include <aipackaging/nesting/polygon_environment.h>
#include <aipackaging/nesting/polygon_solver.h>

#include "polygon_artifact_store_internal.h"

namespace aipackaging::desktop::detail
{
using namespace aipackaging::solver;

namespace
{
/// Переводит микронную точку кольца в миллиметровую точку представления.
PolygonViewPoint toViewPoint(const PolygonPoint64 & point, std::int64_t offsetX, std::int64_t offsetY)
{
  return {static_cast<double>(point.x + offsetX) / 1000.0, static_cast<double>(point.y + offsetY) / 1000.0};
}
} // namespace

/// Выбирает точный вид базового решателя без раскрытия его типа прикладному слою.
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

/// Сопоставляет три этапа общего механизма поиска прикладным этапам хода выполнения.
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

/// Сначала учитывает пользовательскую отмену, затем строго сопоставляет статус решателя.
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

/// Переносит нормализованные кольца размещений в миллиметры и отмечает отсутствующие экземпляры.
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
      part.rotationDegrees = placement.rotationDegrees;
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

/// Извлекает свойства исходных типов и габариты их первой нормализованной ориентации.
PolygonDocumentSummary buildDocumentSummary(const PolygonArtifactStore::DocumentRecord & record)
{
  PolygonDocumentSummary summary;
  summary.sheetWidth = record.problem.sheet.width;
  summary.sheetHeight = record.problem.sheet.height;
  summary.sheetMargin = record.problem.manufacturing.sheetMargin;
  summary.partSpacing = record.problem.manufacturing.partSpacing;
  summary.kerf = record.problem.manufacturing.kerf;
  summary.parts.reserve(record.problem.parts.size());
  for (std::size_t index = 0; index < record.problem.parts.size(); ++index)
  {
    const PolygonPart & source = record.problem.parts[index];
    const auto & orientations = record.environment->orientations(index);
    PolygonPartSummary part;
    part.id = source.id;
    part.quantity = source.quantity;
    part.allowedRotations = source.allowedRotations;
    part.colorIndex = index;
    if (!orientations.empty())
    {
      part.width = static_cast<double>(orientations.front().width) / 1000.0;
      part.height = static_cast<double>(orientations.front().height) / 1000.0;
      part.materialArea = static_cast<double>(orientations.front().materialArea) / 1'000'000.0;
    }
    summary.parts.push_back(std::move(part));
  }
  return summary;
}

/// Заполняет прикладные поля, строит сцену и выдаёт идентификатор только независимо проверенного решения.
NestingRunResult makeRunResult(const std::shared_ptr<PolygonArtifactStore> & store,
                               const std::shared_ptr<const PolygonArtifactStore::DocumentRecord> & record,
                               PolygonDocumentHandle document, PolygonSolution solution, bool cancelled,
                               NestingProvenance provenance, std::string diagnostic)
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
} // namespace aipackaging::desktop::detail
