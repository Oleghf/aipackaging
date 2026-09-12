"""Обработчики CLI-команд генерации и проверки датасетов без ML-зависимостей."""

from __future__ import annotations

from argparse import Namespace
from typing import Any

from ..dataset import DEFAULT_SPLITS, generate_dataset, verify_dataset
from ..polygon_dataset import generate_polygon_dataset, verify_polygon_dataset


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
    """Генерирует polygon_dataset v1 по аргументам CLI и возвращает сводку."""

    sizes = {"train": 1, "validation": 1, "test": 1} if arguments.smoke else None
    keyword = {} if sizes is None else {"split_sizes": sizes}
    manifest = generate_polygon_dataset(
        arguments.output,
        master_seed=arguments.seed,
        workers=arguments.workers,
        **keyword,
    )
    return {"manifest": str(arguments.output / "manifest.json"), "shards": len(manifest["shards"])}


def verify_polygon(arguments: Namespace) -> dict[str, int]:
    """Проверяет polygon_dataset из аргументов CLI и возвращает счётчики записей."""

    return verify_polygon_dataset(arguments.path)
