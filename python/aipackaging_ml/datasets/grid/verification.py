"""Строгая проверка и полный replay клеточного датасета v1."""

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


def verify_dataset(path: str | Path) -> dict[str, int]:
    """Проверяет checksums, split isolation, решения и полный replay всех действий."""

    root = Path(path)
    manifest = read_canonical_json(root / "manifest.json")
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
        raise ValueError("unsupported grid_dataset manifest")
    require_keys(manifest["generator"], {"name", "version", "tiers", "splitSizes"}, "generator")
    require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )

    problem_records: dict[str, tuple[str, dict[str, Any]]] = {}
    trajectory_records: dict[str, dict[str, Any]] = {}
    family_splits: dict[str, str] = {}
    for shard in manifest["shards"]:
        require_keys(shard, {"tier", "split", "kind", "path", "records", "sha256"}, "shard")
        shard_path = resolve_dataset_path(root, shard["path"])
        if sha256_file(shard_path) != shard["sha256"]:
            raise ValueError(f"checksum mismatch: {shard['path']}")
        records = read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"record count mismatch: {shard['path']}")
        if shard["kind"] == "problems":
            for problem in records:
                problem_id = problem["problemId"]
                if problem_id in problem_records:
                    raise ValueError(f"duplicate problemId: {problem_id}")
                signature = family_hash(problem)
                owner = family_splits.setdefault(signature, shard["split"])
                if owner != shard["split"]:
                    raise ValueError(f"family split leakage: {problem_id}")
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
                    raise ValueError("unsupported grid_trajectory record")
                trajectory_id = trajectory["trajectoryId"]
                if trajectory_id in trajectory_records:
                    raise ValueError(f"duplicate trajectoryId: {trajectory_id}")
                trajectory_records[trajectory_id] = trajectory
        else:
            raise ValueError(f"unknown shard kind: {shard['kind']}")

    _verify_trajectories(problem_records, trajectory_records)
    _verify_experts(manifest["expertTrajectoryId"], problem_records, trajectory_records)
    return {
        "problems": len(problem_records),
        "trajectories": len(trajectory_records),
        "families": len(family_splits),
    }


def _verify_trajectories(
    problems: dict[str, tuple[str, dict[str, Any]]], trajectories: dict[str, dict[str, Any]]
) -> None:
    """Повторно проверяет решение и каждый переход всех grid-траекторий."""

    for trajectory_id, trajectory in trajectories.items():
        if trajectory["problemId"] not in problems:
            raise ValueError(f"unknown trajectory problemId: {trajectory_id}")
        problem = problems[trajectory["problemId"]][1]
        problem_json = canonical_json(problem)
        solution = trajectory["finalSolution"]
        if trajectory["solver"] != solution["solver"]:
            raise ValueError(f"solver metadata mismatch: {trajectory_id}")
        if [step["action"] for step in trajectory["steps"]] != solution["placements"]:
            raise ValueError(f"placement replay mismatch: {trajectory_id}")
        error = _native.validate_solution(problem_json, canonical_json(solution))
        if error:
            raise ValueError(f"{trajectory_id}: {error}")

        environment = GridNestingEnv.from_dict(problem)
        environment.reset()
        if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
            raise ValueError(f"action index audit mismatch: {trajectory_id}")
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
                raise ValueError(f"action audit mismatch: {trajectory_id}")
            _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
            if (
                abs(reward - step["reward"]) > 1e-15
                or info["rankBefore"] != step["rankBefore"]
                or info["rankAfter"] != step["rankAfter"]
                or info["deltas"] != step["deltas"]
            ):
                raise ValueError(f"reward replay mismatch: {trajectory_id}")
            if terminated != step["terminated"] or truncated != step["truncated"]:
                raise ValueError(f"termination replay mismatch: {trajectory_id}")


def _verify_experts(
    experts: dict[str, str],
    problems: dict[str, tuple[str, dict[str, Any]]],
    trajectories: dict[str, dict[str, Any]],
) -> None:
    """Проверяет полноту ссылок expert и повторяет выбор общим C++-компаратором."""

    for problem_id, expert_id in experts.items():
        if problem_id not in problems or expert_id not in trajectories:
            raise ValueError(f"broken expertTrajectoryId: {problem_id}")
        candidates = [item for item in trajectories.values() if item["problemId"] == problem_id]
        if len(candidates) != len(SOLVERS) or {item["solver"]["name"] for item in candidates} != set(SOLVERS):
            raise ValueError(f"baseline trajectory set mismatch: {problem_id}")
        actual = best_trajectory(candidates)["trajectoryId"]
        if actual != expert_id:
            raise ValueError(f"wrong expertTrajectoryId: {problem_id}")
    if set(experts) != set(problems):
        raise ValueError("expertTrajectoryId keys do not match problem set")
