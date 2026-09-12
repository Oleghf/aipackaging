"""Воспроизводимый benchmark замороженных полигональных baseline-траекторий."""

from __future__ import annotations

from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Mapping

from ... import _aipackaging_solver as _native
from ..serialization import canonical_json, read_canonical_json, sha256_file, write_canonical_json
from .rollout import POLYGON_SOLVERS
from .verification import load_polygon_dataset_records


def _objective_view(solution: Mapping[str, Any]) -> dict[str, Any]:
    """Извлекает все стабильные компоненты качества без диагностического времени."""

    objective = solution["objective"]
    return {
        "placedParts": objective["placedParts"],
        "totalParts": objective["totalParts"],
        "placedAreaSquareMicrometers": objective["placedAreaSquareMicrometers"],
        "usedLengthMicrometers": objective["usedLengthMicrometers"],
        "largestExtraRectangleSquareMicrometers": objective["largestExtraRectangleSquareMicrometers"],
        "fragmentationPenaltySquareMicrometers": objective["fragmentationPenaltySquareMicrometers"],
        "materialUtilization": objective["materialUtilization"],
    }


def _mean(total: int | float, count: int) -> float:
    """Возвращает устойчивое среднее либо ноль для пустой выборки."""

    return float(total) / count if count else 0.0


def _quality_relation(candidate: Mapping[str, Any], expert: Mapping[str, Any]) -> str:
    """Различает равное лучшему и строго худшее качество общим C++-компаратором."""

    candidate_wire = canonical_json(candidate["finalSolution"])
    expert_wire = canonical_json(expert["finalSolution"])
    if _native.is_better_polygon_solution(candidate_wire, expert_wire):
        raise ValueError("frozen expert is not the best polygon trajectory")
    if _native.is_better_polygon_solution(expert_wire, candidate_wire):
        return "worse_than_best"
    return "equivalent_to_best"


def _build_report(root: Path, split: str) -> dict[str, Any]:
    """Агрегирует только сохранённые trajectories выбранного split без запуска solver."""

    manifest, problems, trajectories = load_polygon_dataset_records(root, split)
    by_problem: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for trajectory in trajectories:
        by_problem[trajectory["problemId"]].append(trajectory)

    expert_counts: Counter[str] = Counter()
    totals: dict[str, Counter[str]] = {solver: Counter() for solver in POLYGON_SOLVERS}
    tasks = []
    for problem in problems:
        problem_id = problem["problemId"]
        expert_id = manifest["expertTrajectoryId"][problem_id]
        results = sorted(by_problem[problem_id], key=lambda item: POLYGON_SOLVERS.index(item["solver"]["name"]))
        expert = next((item for item in results if item["trajectoryId"] == expert_id), None)
        if expert is None:
            raise ValueError(f"missing expert trajectory: {problem_id}")
        expert_solver = expert["solver"]["name"]
        expert_counts[expert_solver] += 1
        expert_used = expert["finalSolution"]["objective"]["usedLengthMicrometers"]
        task_results = []
        for trajectory in results:
            solver = trajectory["solver"]["name"]
            solution = trajectory["finalSolution"]
            objective = solution["objective"]
            totals[solver]["tasks"] += 1
            totals[solver]["solved"] += solution["status"] == "solved"
            totals[solver]["placedParts"] += objective["placedParts"]
            totals[solver]["placedArea"] += objective["placedAreaSquareMicrometers"]
            totals[solver]["usedLength"] += objective["usedLengthMicrometers"]
            totals[solver]["largestExtra"] += objective["largestExtraRectangleSquareMicrometers"]
            totals[solver]["fragmentation"] += objective["fragmentationPenaltySquareMicrometers"]
            totals[solver]["utilization"] += objective["materialUtilization"]
            task_results.append({
                "solver": solver,
                "status": solution["status"],
                "objective": _objective_view(solution),
                "isExpert": trajectory["trajectoryId"] == expert_id,
                "qualityRelation": _quality_relation(trajectory, expert),
                "usedLengthDeltaFromExpertMicrometers": objective["usedLengthMicrometers"] - expert_used,
            })
        metadata = manifest.get("problems", {}).get(problem_id, {})
        tasks.append({
            "problemId": problem_id,
            "tier": metadata.get("tier", "smoke"),
            "expertTrajectoryId": expert_id,
            "expertSolver": expert_solver,
            "results": task_results,
        })

    summaries = {}
    for solver in POLYGON_SOLVERS:
        values = totals[solver]
        count = values["tasks"]
        summaries[solver] = {
            "tasks": count,
            "solved": values["solved"],
            "completionRate": _mean(values["solved"], count),
            "meanPlacedParts": _mean(values["placedParts"], count),
            "meanPlacedAreaSquareMicrometers": _mean(values["placedArea"], count),
            "meanUsedLengthMicrometers": _mean(values["usedLength"], count),
            "meanLargestExtraRectangleSquareMicrometers": _mean(values["largestExtra"], count),
            "meanFragmentationPenaltySquareMicrometers": _mean(values["fragmentation"], count),
            "meanMaterialUtilization": _mean(values["utilization"], count),
            "expertSelections": expert_counts[solver],
        }
    return {
        "format": "aipackaging.polygon_benchmark_report",
        "version": 1,
        "datasetManifestSha256": sha256_file(root / "manifest.json"),
        "datasetVersion": manifest["version"],
        "revision": manifest["revision"],
        "split": split,
        "solverBudgets": manifest["solverBudgets"],
        "summaries": summaries,
        "tasks": tasks,
    }


def benchmark_polygon_baselines(
    dataset: str | Path, output: str | Path, *, split: str = "validation"
) -> dict[str, Any]:
    """Строит canonical benchmark-report из frozen validation либо test split."""

    if split not in {"validation", "test"}:
        raise ValueError("polygon benchmark supports validation or test split")
    report = _build_report(Path(dataset), split)
    write_canonical_json(output, report)
    return report


def verify_polygon_benchmark(dataset: str | Path, report_path: str | Path) -> dict[str, int]:
    """Проверяет строгий формат, dataset hash и все агрегаты benchmark-report."""

    report = read_canonical_json(report_path)
    expected_fields = {
        "format", "version", "datasetManifestSha256", "datasetVersion", "revision", "split",
        "solverBudgets", "summaries", "tasks",
    }
    if set(report) != expected_fields:
        raise ValueError("polygon benchmark report fields mismatch")
    if report["format"] != "aipackaging.polygon_benchmark_report" or report["version"] != 1:
        raise ValueError("unsupported polygon benchmark report")
    if report["split"] not in {"validation", "test"}:
        raise ValueError("polygon benchmark report uses a forbidden split")
    expected = _build_report(Path(dataset), report["split"])
    if report != expected:
        raise ValueError("polygon benchmark report does not match frozen trajectories")
    return {"tasks": len(report["tasks"]), "solvers": len(report["summaries"])}
