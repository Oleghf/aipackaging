"""Сборка воспроизводимых shards и manifest полигонального smoke-датасета v1."""

from __future__ import annotations

import multiprocessing
from pathlib import Path
from typing import Any, Mapping

from ..serialization import sha256_file, write_canonical_json, write_jsonl_gzip
from .generation import generate_polygon_problem
from .rollout import rollout_task

POLYGON_SPLITS = {"train": 16, "validation": 4, "test": 4}


def generate_polygon_dataset(
    output: str | Path,
    *,
    master_seed: int = 42,
    split_sizes: Mapping[str, int] = POLYGON_SPLITS,
    workers: int = 1,
) -> dict[str, Any]:
    """Генерирует polygon_dataset v1 и возвращает записанный manifest."""

    root = Path(output)
    tasks = {
        split: [generate_polygon_problem(master_seed, split, index) for index in range(split_sizes[split])]
        for split in ("train", "validation", "test")
    }
    ordered = [task for split in ("train", "validation", "test") for task in tasks[split]]
    if workers == 1:
        results = [rollout_task(task) for task in ordered]
    else:
        with multiprocessing.get_context("spawn").Pool(workers) as pool:
            results = pool.map(rollout_task, ordered)
    rollouts = {problem_id: (items, expert) for problem_id, items, expert in results}
    shards = []
    experts = {}
    revision = "unknown"
    for split in ("train", "validation", "test"):
        problems = [problem for problem, _ in tasks[split]]
        trajectories = [item for problem in problems for item in rollouts[problem["problemId"]][0]]
        if trajectories:
            revision = trajectories[0]["solver"]["revision"]
        for problem in problems:
            experts[problem["problemId"]] = rollouts[problem["problemId"]][1]
        for kind, path, records in (
            ("problems", root / f"{split}-problems.jsonl.gz", problems),
            ("trajectories", root / f"{split}-trajectories.jsonl.gz", trajectories),
        ):
            write_jsonl_gzip(path, records)
            shards.append(
                {
                    "split": split,
                    "kind": kind,
                    "path": path.relative_to(root).as_posix(),
                    "records": len(records),
                    "sha256": sha256_file(path),
                }
            )
    manifest = {
        "format": "aipackaging.polygon_dataset",
        "version": 1,
        "problemContractVersion": 1,
        "trajectoryContractVersion": 1,
        "observationVersion": 1,
        "masterSeed": master_seed,
        "revision": revision,
        "generator": {
            "name": "deterministic-polygon-smoke",
            "version": 1,
            "splitSizes": dict(split_sizes),
        },
        "solverBudgets": {
            "timeoutMs": 0,
            "randomIterations": 8,
            "beamWidth": 4,
            "maxExpandedStates": 100,
        },
        "shards": shards,
        "expertTrajectoryId": dict(sorted(experts.items())),
    }
    write_canonical_json(root / "manifest.json", manifest)
    return manifest
