"""Строгая проверка и полное повторное проигрывание клеточного набора данных v1."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ... import _aipackaging_solver as _native
from ...environment import GridNestingEnv
from ..serialization import (
    canonical_json,
    read_canonical_json,
    read_jsonl_gzip,
    require_keys,
    resolve_dataset_path,
    sha256_file,
)
from .generation import family_hash
from .rollout import SOLVERS, best_trajectory


def _verify_grid_manifest(manifest: dict[str, Any]) -> None:
    """Проверяет корневой контракт, генератор и бюджеты клеточного набора."""

    require_keys(
        manifest,
        {
            "format",
            "version",
            "problemContractVersion",
            "trajectoryContractVersion",
            "observationVersion",
            "rewardVersion",
            "masterSeed",
            "revision",
            "generator",
            "solverBudgets",
            "shards",
            "expertTrajectoryId",
        },
        "manifest",
    )
    if manifest.get("format") != "aipackaging.grid_dataset" or manifest.get("version") != 1:
        raise ValueError("неподдерживаемый манифест `grid_dataset`")
    require_keys(manifest["generator"], {"name", "version", "tiers", "splitSizes"}, "generator")
    require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )


def _load_grid_shards(
    root: Path, manifest: dict[str, Any]
) -> tuple[dict[str, tuple[str, dict[str, Any]]], dict[str, dict[str, Any]], int]:
    """Проверяет части, уникальность записей и изоляцию клеточных семейств."""

    problem_records: dict[str, tuple[str, dict[str, Any]]] = {}
    trajectory_records: dict[str, dict[str, Any]] = {}
    family_splits: dict[str, str] = {}
    for shard in manifest["shards"]:
        require_keys(shard, {"tier", "split", "kind", "path", "records", "sha256"}, "shard")
        shard_path = resolve_dataset_path(root, shard["path"])
        if sha256_file(shard_path) != shard["sha256"]:
            raise ValueError(f"контрольная сумма не совпадает: {shard['path']}")
        records = read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"число записей не совпадает: {shard['path']}")
        if shard["kind"] == "problems":
            for problem in records:
                problem_id = problem["problemId"]
                if problem_id in problem_records:
                    raise ValueError(f"повторяющийся problemId: {problem_id}")
                signature = family_hash(problem)
                owner = family_splits.setdefault(signature, shard["split"])
                if owner != shard["split"]:
                    raise ValueError(f"семейство попало в разные выборки: {problem_id}")
                problem_records[problem_id] = (shard["split"], problem)
        elif shard["kind"] == "trajectories":
            for trajectory in records:
                require_keys(
                    trajectory,
                    {
                        "format",
                        "version",
                        "trajectoryId",
                        "problemId",
                        "solver",
                        "actionIndices",
                        "steps",
                        "finalSolution",
                    },
                    "trajectory",
                )
                if trajectory["format"] != "aipackaging.grid_trajectory" or trajectory["version"] != 1:
                    raise ValueError("неподдерживаемая запись `grid_trajectory`")
                trajectory_id = trajectory["trajectoryId"]
                if trajectory_id in trajectory_records:
                    raise ValueError(f"повторяющийся trajectoryId: {trajectory_id}")
                trajectory_records[trajectory_id] = trajectory
        else:
            raise ValueError(f"неизвестный вид части набора: {shard['kind']}")
    return problem_records, trajectory_records, len(family_splits)


def verify_dataset(path: str | Path) -> dict[str, int]:
    """Проверяет контрольные суммы, изоляцию выборок, решения и повтор всех действий."""

    root = Path(path)
    manifest = read_canonical_json(root / "manifest.json")
    _verify_grid_manifest(manifest)
    problems, trajectories, family_count = _load_grid_shards(root, manifest)
    _verify_trajectories(problems, trajectories)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories)
    return {
        "problems": len(problems),
        "trajectories": len(trajectories),
        "families": family_count,
    }


def _verify_trajectories(
    problems: dict[str, tuple[str, dict[str, Any]]], trajectories: dict[str, dict[str, Any]]
) -> None:
    """Повторно проверяет решение и каждый переход всех клеточных траекторий."""

    for trajectory_id, trajectory in trajectories.items():
        if trajectory["problemId"] not in problems:
            raise ValueError(f"неизвестный problemId траектории: {trajectory_id}")
        problem = problems[trajectory["problemId"]][1]
        problem_json = canonical_json(problem)
        solution = trajectory["finalSolution"]
        if trajectory["solver"] != solution["solver"]:
            raise ValueError(f"метаданные решателя не совпадают: {trajectory_id}")
        if [step["action"] for step in trajectory["steps"]] != solution["placements"]:
            raise ValueError(f"размещения повторного проигрывания не совпадают: {trajectory_id}")
        error = _native.validate_solution(problem_json, canonical_json(solution))
        if error:
            raise ValueError(f"{trajectory_id}: {error}")

        environment = GridNestingEnv.from_dict(problem)
        environment.reset()
        if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
            raise ValueError(f"проверочные индексы действий не совпадают: {trajectory_id}")
        for step in trajectory["steps"]:
            require_keys(
                step,
                {
                    "actionIndex",
                    "action",
                    "reward",
                    "rankBefore",
                    "rankAfter",
                    "deltas",
                    "terminated",
                    "truncated",
                },
                "trajectory step",
            )
            if environment.action(step["actionIndex"]) != step["action"]:
                raise ValueError(f"проверочные действия не совпадают: {trajectory_id}")
            _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
            if (
                abs(reward - step["reward"]) > 1e-15
                or info["rankBefore"] != step["rankBefore"]
                or info["rankAfter"] != step["rankAfter"]
                or info["deltas"] != step["deltas"]
            ):
                raise ValueError(f"вознаграждение повторного проигрывания не совпадает: {trajectory_id}")
            if terminated != step["terminated"] or truncated != step["truncated"]:
                raise ValueError(f"завершение повторного проигрывания не совпадает: {trajectory_id}")


def _verify_experts(
    experts: dict[str, str],
    problems: dict[str, tuple[str, dict[str, Any]]],
    trajectories: dict[str, dict[str, Any]],
) -> None:
    """Проверяет полноту экспертных ссылок и повторяет выбор общим компаратором C++."""

    for problem_id, expert_id in experts.items():
        if problem_id not in problems or expert_id not in trajectories:
            raise ValueError(f"повреждённое значение expertTrajectoryId: {problem_id}")
        candidates = [item for item in trajectories.values() if item["problemId"] == problem_id]
        if len(candidates) != len(SOLVERS) or {item["solver"]["name"] for item in candidates} != set(SOLVERS):
            raise ValueError(f"набор траекторий базовых алгоритмов не совпадает: {problem_id}")
        actual = best_trajectory(candidates)["trajectoryId"]
        if actual != expert_id:
            raise ValueError(f"неверное значение expertTrajectoryId: {problem_id}")
    if set(experts) != set(problems):
        raise ValueError("ключи expertTrajectoryId не соответствуют набору задач")
