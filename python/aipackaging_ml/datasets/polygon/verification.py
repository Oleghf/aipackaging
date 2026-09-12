"""Строгая проверка полигонального dataset v1 и его динамических trajectories."""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from ..serialization import (
    canonical_json,
    read_canonical_json,
    read_jsonl_gzip,
    require_keys,
    resolve_dataset_path,
    sha256_file,
)
from .replay import verify_replay
from .rollout import POLYGON_SOLVERS, best_trajectory


def verify_polygon_dataset(path: str | Path) -> dict[str, int]:
    """Проверяет checksums, strict решения и динамический replay всех траекторий."""

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
            "masterSeed",
            "revision",
            "generator",
            "solverBudgets",
            "shards",
            "expertTrajectoryId",
        },
        "manifest",
    )
    if manifest.get("format") != "aipackaging.polygon_dataset" or manifest.get("version") != 1:
        raise ValueError("unsupported polygon_dataset manifest")
    require_keys(manifest["generator"], {"name", "version", "splitSizes"}, "generator")
    require_keys(manifest["generator"]["splitSizes"], {"train", "validation", "test"}, "split sizes")
    require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )

    problems: dict[str, dict[str, Any]] = {}
    trajectories: dict[str, dict[str, Any]] = {}
    family_splits: dict[str, str] = {}
    for shard in manifest["shards"]:
        require_keys(shard, {"split", "kind", "path", "records", "sha256"}, "shard")
        if shard["split"] not in {"train", "validation", "test"} or shard["kind"] not in {
            "problems",
            "trajectories",
        }:
            raise ValueError(f"invalid shard discriminator: {shard['path']}")
        shard_path = resolve_dataset_path(root, shard["path"])
        if sha256_file(shard_path) != shard["sha256"]:
            raise ValueError(f"invalid shard: {shard['path']}")
        records = read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"record count mismatch: {shard['path']}")
        target = problems if shard["kind"] == "problems" else trajectories
        key = "problemId" if shard["kind"] == "problems" else "trajectoryId"
        for record in records:
            if shard["kind"] == "trajectories":
                require_keys(
                    record,
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
                if record["format"] != "aipackaging.polygon_trajectory" or record["version"] != 1:
                    raise ValueError("unsupported polygon_trajectory")
            else:
                signature_source = {
                    "sheet": record["sheet"],
                    "manufacturing": record["manufacturing"],
                    "parts": record["parts"],
                }
                signature = hashlib.sha256(canonical_json(signature_source).encode()).hexdigest()
                owner = family_splits.setdefault(signature, shard["split"])
                if owner != shard["split"]:
                    raise ValueError(f"polygon family split leakage: {record['problemId']}")
            if record[key] in target:
                raise ValueError(f"duplicate {key}: {record[key]}")
            target[record[key]] = record

    for trajectory in trajectories.values():
        if trajectory["problemId"] not in problems:
            raise ValueError(f"unknown trajectory problemId: {trajectory['trajectoryId']}")
        verify_replay(problems[trajectory["problemId"]], trajectory)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories)
    return {"problems": len(problems), "trajectories": len(trajectories)}


def _verify_experts(
    experts: dict[str, str], problems: dict[str, dict[str, Any]], trajectories: dict[str, dict[str, Any]]
) -> None:
    """Проверяет покрытие problem→expert и повторяет выбор лучшей траектории."""

    if set(experts) != set(problems):
        raise ValueError("expertTrajectoryId keys mismatch")
    for problem_id, expert_id in experts.items():
        candidates = [value for value in trajectories.values() if value["problemId"] == problem_id]
        if len(candidates) != len(POLYGON_SOLVERS) or {
            item["solver"]["name"] for item in candidates
        } != set(POLYGON_SOLVERS):
            raise ValueError(f"baseline trajectory set mismatch: {problem_id}")
        if best_trajectory(candidates)["trajectoryId"] != expert_id:
            raise ValueError(f"wrong expertTrajectoryId: {problem_id}")
