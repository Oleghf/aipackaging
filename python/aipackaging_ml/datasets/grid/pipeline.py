"""Сборка воспроизводимых shards и manifest клеточного датасета v1."""

from __future__ import annotations

import multiprocessing
from pathlib import Path
from typing import Any, Mapping

from ..serialization import sha256_file, write_canonical_json, write_jsonl_gzip
from .generation import PROFILES, generate_unique_problem
from .rollout import rollout_task

DEFAULT_SPLITS = {"train": 256, "validation": 64, "test": 64}


def generate_dataset(
    output: str | Path,
    *,
    master_seed: int = 42,
    tiers: tuple[str, ...] = ("small", "medium"),
    split_sizes: Mapping[str, int] = DEFAULT_SPLITS,
    workers: int = 1,
) -> dict[str, Any]:
    """Генерирует все shards и возвращает записанный manifest grid_dataset v1."""

    output_path = Path(output)
    output_path.mkdir(parents=True, exist_ok=True)
    family_splits: dict[str, str] = {}
    grouped: dict[tuple[str, str], list[tuple[dict[str, Any], int]]] = {}
    for tier in tiers:
        for split in ("train", "validation", "test"):
            tasks = []
            for index in range(split_sizes[split]):
                tasks.append(generate_unique_problem(master_seed, tier, split, index, family_splits))
            grouped[(tier, split)] = tasks

    ordered_tasks = [task for key in sorted(grouped) for task in grouped[key]]
    if workers == 1:
        rollout_results = [rollout_task(task) for task in ordered_tasks]
    else:
        # spawn обеспечивает одинаковую модель процессов на Windows, Linux и macOS.
        context = multiprocessing.get_context("spawn")
        with context.Pool(processes=workers) as pool:
            rollout_results = pool.map(rollout_task, ordered_tasks)
    rollouts = {problem_id: (trajectories, expert) for problem_id, trajectories, expert in rollout_results}

    shards = []
    experts: dict[str, str] = {}
    revision = "unknown"
    for (tier, split), tasks in sorted(grouped.items()):
        problems = [problem for problem, _ in tasks]
        trajectories = [item for problem in problems for item in rollouts[problem["problemId"]][0]]
        for problem in problems:
            experts[problem["problemId"]] = rollouts[problem["problemId"]][1]
        if trajectories:
            revision = trajectories[0]["solver"]["revision"]

        problem_path = output_path / tier / f"{split}-problems.jsonl.gz"
        trajectory_path = output_path / tier / f"{split}-trajectories.jsonl.gz"
        write_jsonl_gzip(problem_path, problems)
        write_jsonl_gzip(trajectory_path, trajectories)
        for kind, path, count in (
            ("problems", problem_path, len(problems)),
            ("trajectories", trajectory_path, len(trajectories)),
        ):
            shards.append(
                {
                    "tier": tier,
                    "split": split,
                    "kind": kind,
                    "path": path.relative_to(output_path).as_posix(),
                    "records": count,
                    "sha256": sha256_file(path),
                }
            )

    profiles = {
        tier: {
            "sheetDimensions": [PROFILES[tier].sheet_min, PROFILES[tier].sheet_max],
            "typeCount": [PROFILES[tier].type_min, PROFILES[tier].type_max],
            "instanceCount": [PROFILES[tier].instance_min, PROFILES[tier].instance_max],
            "partArea": [PROFILES[tier].part_area_min, PROFILES[tier].part_area_max],
            "utilization": [PROFILES[tier].utilization_min, PROFILES[tier].utilization_max],
        }
        for tier in tiers
    }
    manifest = {
        "format": "aipackaging.grid_dataset",
        "version": 1,
        "problemContractVersion": 1,
        "trajectoryContractVersion": 1,
        "observationVersion": 1,
        "rewardVersion": 1,
        "masterSeed": master_seed,
        "revision": revision,
        "generator": {
            "name": "seeded-polyomino-growth",
            "version": 1,
            "tiers": profiles,
            "splitSizes": dict(split_sizes),
        },
        "solverBudgets": {
            "timeoutMs": 0,
            "randomIterations": 64,
            "beamWidth": 32,
            "maxExpandedStates": 50_000,
        },
        "shards": shards,
        "expertTrajectoryId": dict(sorted(experts.items())),
    }
    write_canonical_json(output_path / "manifest.json", manifest)
    return manifest
