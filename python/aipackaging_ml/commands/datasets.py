"""Обработчики CLI-команд генерации и проверки датасетов без ML-зависимостей."""

from __future__ import annotations

from argparse import Namespace
from typing import Any

from ..dataset import DEFAULT_SPLITS, generate_dataset, verify_dataset
from ..polygon_benchmark import benchmark_polygon_baselines
from ..polygon_dataset import (
    POLYGON_BUDGETS,
    POLYGON_SMOKE_BUDGETS,
    POLYGON_SMOKE_SPLITS,
    POLYGON_SPLITS,
    generate_polygon_dataset,
    verify_polygon_dataset,
)


def generate_grid(arguments: Namespace) -> dict[str, Any]:
    """Генерирует grid_dataset по разобранным аргументам CLI и возвращает сводку."""

    split_sizes = (
        {"train": 1, "validation": 1, "test": 1}
        if arguments.smoke
        else {"train": arguments.train, "validation": arguments.validation, "test": arguments.test}
    )
    manifest = generate_dataset(
        arguments.output,
        master_seed=arguments.seed,
        tiers=tuple(arguments.tier or ("small", "medium")),
        split_sizes=split_sizes,
        workers=arguments.workers,
    )
    return {"manifest": str(arguments.output / "manifest.json"), "shards": len(manifest["shards"])}


def verify_grid(arguments: Namespace) -> dict[str, int]:
    """Проверяет grid_dataset из аргументов CLI и возвращает счётчики записей."""

    return verify_dataset(arguments.path)


def generate_polygon(arguments: Namespace) -> dict[str, Any]:
    """Генерирует polygon_dataset v2 по аргументам CLI и возвращает сводку."""

    sizes = (
        POLYGON_SMOKE_SPLITS
        if arguments.smoke
        else {"train": arguments.train, "validation": arguments.validation, "test": arguments.test}
    )
    budgets = (
        POLYGON_SMOKE_BUDGETS
        if arguments.smoke
        else {
            "timeoutMs": 0,
            "randomIterations": arguments.random_iterations,
            "beamWidth": arguments.beam_width,
            "maxExpandedStates": arguments.max_expanded_states,
        }
    )
    tiers = ("small", "medium") if arguments.smoke else tuple(arguments.tier or ("small", "medium"))
    canonical = (
        arguments.seed == 42
        and sizes == POLYGON_SPLITS
        and tiers == ("small", "medium")
        and budgets == POLYGON_BUDGETS
        and arguments.max_attempts == 256
    )
    manifest = generate_polygon_dataset(
        arguments.output,
        master_seed=arguments.seed,
        split_sizes=sizes,
        tiers=tiers,
        workers=arguments.workers,
        budgets=budgets,
        max_attempts=arguments.max_attempts,
        resume=arguments.resume,
        mode="smoke" if arguments.smoke else "canonical" if canonical else "custom",
    )
    return {"manifest": str(arguments.output / "manifest.json"), "shards": len(manifest["shards"])}


def verify_polygon(arguments: Namespace) -> dict[str, int]:
    """Проверяет polygon_dataset из аргументов CLI и возвращает счётчики записей."""

    return verify_polygon_dataset(arguments.path)


def benchmark_polygon(arguments: Namespace) -> dict[str, Any]:
    """Строит benchmark по frozen polygon trajectories без повторного запуска solver."""

    report = benchmark_polygon_baselines(arguments.dataset, arguments.output, split=arguments.split)
    return {"output": str(arguments.output), "tasks": len(report["tasks"])}
