"""Сборка воспроизводимых частей и манифеста клеточного набора данных v1."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

from ..pipeline import collect_by_key, write_shard
from ..serialization import write_canonical_json
from .generation import PROFILES, generate_unique_problem
from .rollout import rollout_task

DEFAULT_SPLITS = {"train": 256, "validation": 64, "test": 64}

GridTask = tuple[dict[str, Any], int]
GridRollout = tuple[str, list[dict[str, Any]], str]


def _plan_grid_tasks(
    master_seed: int, tiers: tuple[str, ...], split_sizes: Mapping[str, int]
) -> dict[tuple[str, str], list[GridTask]]:
    """Строит задачи генерации в прежнем порядке профилей и выборок."""

    family_splits: dict[str, str] = {}
    grouped: dict[tuple[str, str], list[GridTask]] = {}
    for tier in tiers:
        for split in ("train", "validation", "test"):
            grouped[(tier, split)] = [
                generate_unique_problem(master_seed, tier, split, index, family_splits)
                for index in range(split_sizes[split])
            ]
    return grouped


def _execute_grid_rollouts(
    grouped: Mapping[tuple[str, str], list[GridTask]], workers: int
) -> dict[str, tuple[list[dict[str, Any]], str]]:
    """Выполняет базовые алгоритмы и индексирует результат по задаче."""

    ordered_tasks = [task for key in sorted(grouped) for task in grouped[key]]
    results = collect_by_key(rollout_task, ordered_tasks, workers, lambda item: item[0])
    return {problem_id: (trajectories, expert) for problem_id, trajectories, expert in results.values()}


def _publish_grid_shards(
    output: Path,
    grouped: Mapping[tuple[str, str], list[GridTask]],
    rollouts: Mapping[str, tuple[list[dict[str, Any]], str]],
) -> tuple[list[dict[str, Any]], dict[str, str], str]:
    """Записывает части набора и собирает экспертные ссылки и ревизию."""

    shards: list[dict[str, Any]] = []
    experts: dict[str, str] = {}
    revision = "unknown"
    for (tier, split), tasks in sorted(grouped.items()):
        problems = [problem for problem, _ in tasks]
        trajectories = [item for problem in problems for item in rollouts[problem["problemId"]][0]]
        for problem in problems:
            experts[problem["problemId"]] = rollouts[problem["problemId"]][1]
        if trajectories:
            revision = trajectories[0]["solver"]["revision"]
        shards.append(
            write_shard(
                output,
                output / tier / f"{split}-problems.jsonl.gz",
                problems,
                split=split,
                kind="problems",
                metadata={"tier": tier},
            )
        )
        shards.append(
            write_shard(
                output,
                output / tier / f"{split}-trajectories.jsonl.gz",
                trajectories,
                split=split,
                kind="trajectories",
                metadata={"tier": tier},
            )
        )
    return shards, experts, revision


def _grid_profiles(tiers: tuple[str, ...]) -> dict[str, dict[str, list[float | int]]]:
    """Преобразует профили генератора в прежнее представление манифеста."""

    return {
        tier: {
            "sheetDimensions": [PROFILES[tier].sheet_min, PROFILES[tier].sheet_max],
            "typeCount": [PROFILES[tier].type_min, PROFILES[tier].type_max],
            "instanceCount": [PROFILES[tier].instance_min, PROFILES[tier].instance_max],
            "partArea": [PROFILES[tier].part_area_min, PROFILES[tier].part_area_max],
            "utilization": [PROFILES[tier].utilization_min, PROFILES[tier].utilization_max],
        }
        for tier in tiers
    }


def _build_grid_manifest(
    *,
    master_seed: int,
    tiers: tuple[str, ...],
    split_sizes: Mapping[str, int],
    revision: str,
    shards: list[dict[str, Any]],
    experts: Mapping[str, str],
) -> dict[str, Any]:
    """Собирает клеточный манифест v1 без выполнения ввода-вывода."""

    return {
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
            "tiers": _grid_profiles(tiers),
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


def generate_dataset(
    output: str | Path,
    *,
    master_seed: int = 42,
    tiers: tuple[str, ...] = ("small", "medium"),
    split_sizes: Mapping[str, int] = DEFAULT_SPLITS,
    workers: int = 1,
) -> dict[str, Any]:
    """Создаёт все части и возвращает записанный манифест `grid_dataset` v1."""

    output_path = Path(output)
    output_path.mkdir(parents=True, exist_ok=True)
    grouped = _plan_grid_tasks(master_seed, tiers, split_sizes)
    rollouts = _execute_grid_rollouts(grouped, workers)
    shards, experts, revision = _publish_grid_shards(output_path, grouped, rollouts)
    manifest = _build_grid_manifest(
        master_seed=master_seed,
        tiers=tiers,
        split_sizes=split_sizes,
        revision=revision,
        shards=shards,
        experts=experts,
    )
    write_canonical_json(output_path / "manifest.json", manifest)
    return manifest
