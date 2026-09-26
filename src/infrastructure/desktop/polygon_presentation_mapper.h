#ifndef AIPACKAGING_INFRASTRUCTURE_POLYGON_PRESENTATION_MAPPER_H
#define AIPACKAGING_INFRASTRUCTURE_POLYGON_PRESENTATION_MAPPER_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <polygon_artifact_store.h>

namespace aipackaging::solver
{
enum class SearchProgressStage : std::uint8_t;
enum class SolveStatus : std::uint8_t;
enum class SolverKind : std::uint8_t;
struct PolygonSolution;
} // namespace aipackaging::solver

namespace aipackaging::desktop::detail
{
/// Преобразует прикладное имя базового алгоритма в контракт поискового модуля.
aipackaging::solver::SolverKind toSolverKind(BaselineAlgorithm algorithm);
/// Преобразует этап поискового модуля в независимый прикладной этап.
NestingProgressStage toProgressStage(aipackaging::solver::SearchProgressStage stage);
/// Преобразует статус решателя и признак отмены в причину завершения сценария.
NestingCompletion toCompletion(aipackaging::solver::SolveStatus status, bool cancelled);
/// Формирует сцену и перечень неразмещённых экземпляров без регистрации решения.
void buildPresentation(const PolygonArtifactStore::DocumentRecord & record, const aipackaging::solver::PolygonSolution * solution,
                       PolygonSceneView & scene, std::vector<std::string> & unplaced);
/// Формирует нейтральное описание листа и типов деталей для прикладного снимка.
PolygonDocumentSummary buildDocumentSummary(const PolygonArtifactStore::DocumentRecord & record);
/// Преобразует проверяемое решение в прикладной результат и регистрирует сохраняемый идентификатор.
NestingRunResult makeRunResult(const std::shared_ptr<PolygonArtifactStore> & store,
                               const std::shared_ptr<const PolygonArtifactStore::DocumentRecord> & record,
                               PolygonDocumentHandle document, aipackaging::solver::PolygonSolution solution, bool cancelled,
                               NestingProvenance provenance, std::string diagnostic = {});
} // namespace aipackaging::desktop::detail

#endif
