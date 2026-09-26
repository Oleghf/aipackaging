#!/usr/bin/env python3
"""Вычисляет устойчивые хеши каталогов действий на замороженных траекториях."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Mapping


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "python"))

from aipackaging_ml.datasets.serialization import (  # noqa: E402
    canonical_line,
    read_canonical_json,
    read_jsonl_gzip,
    resolve_dataset_path,
    sha256_file,
    write_canonical_json,
)
from aipackaging_ml.environment import PolygonNestingEnv  # noqa: E402


def _load_records(dataset_root: Path, manifest: Mapping[str, Any]) -> tuple[dict[str, dict[str, Any]], list[dict[str, Any]]]:
    """Читает задачи и траектории в стабильном порядке манифеста."""

    problems: dict[str, dict[str, Any]] = {}
    trajectories: list[dict[str, Any]] = []
    for shard in manifest["shards"]:
        records = read_jsonl_gzip(resolve_dataset_path(dataset_root, shard["path"]))
        if shard["kind"] == "problems":
            for problem in records:
                problems[problem["problemId"]] = problem
        elif shard["kind"] == "trajectories":
            trajectories.extend(records)
    trajectories.sort(key=lambda item: item["trajectoryId"])
    return problems, trajectories


def _catalog_index(actions: list[dict[str, Any]], expected: Mapping[str, Any], label: str) -> int:
    """Возвращает индекс точного действия либо завершает проверку с диагностикой."""

    target = dict(expected)
    try:
        return actions.index(target)
    except ValueError as error:
        raise ValueError(f"{label}: действие отсутствует в каталоге") from error


def _hash_trajectory(arguments: tuple[dict[str, Any], dict[str, Any], int]) -> dict[str, Any]:
    """Хеширует каталоги одной траектории и возвращает малую устойчивую сводку."""

    problem, trajectory, catalog_version = arguments
    digest = hashlib.sha256()
    state_count = 0
    action_count = 0
    trajectory_id = trajectory["trajectoryId"]
    environment = PolygonNestingEnv.from_dict(problem, reward_version=1, catalog_version=catalog_version)
    environment.reset()
    for step_index, step in enumerate(trajectory["steps"]):
        actions = environment.actions()
        digest.update(
            canonical_line(
                {
                    "actions": actions,
                    "catalogVersion": catalog_version,
                    "step": step_index,
                    "trajectoryId": trajectory_id,
                }
            )
        )
        state_count += 1
        action_count += len(actions)
        if catalog_version == 1:
            action_index = step["actionIndex"]
            if action_index >= len(actions) or actions[action_index] != step["action"]:
                raise ValueError(f"{trajectory_id}:{step_index}: индекс каталога v1 не совпадает")
        else:
            action_index = _catalog_index(actions, step["action"], f"{trajectory_id}:{step_index}")
        environment.step_compact(action_index)

    actions = environment.actions()
    digest.update(
        canonical_line(
            {
                "actions": actions,
                "catalogVersion": catalog_version,
                "step": len(trajectory["steps"]),
                "trajectoryId": trajectory_id,
            }
        )
    )
    return {
        "actionCount": action_count + len(actions),
        "sha256": digest.hexdigest(),
        "stateCount": state_count + 1,
        "trajectoryId": trajectory_id,
    }


def _hash_version(
    problems: Mapping[str, dict[str, Any]], trajectories: list[dict[str, Any]], catalog_version: int, workers: int
) -> dict[str, Any]:
    """Собирает хеш версии из сводок траекторий в стабильном порядке."""

    jobs = [(problems[trajectory["problemId"]], trajectory, catalog_version) for trajectory in trajectories]
    executor: concurrent.futures.ProcessPoolExecutor | None = None
    if workers == 1:
        results = map(_hash_trajectory, jobs)
    else:
        executor = concurrent.futures.ProcessPoolExecutor(max_workers=workers)
        results = executor.map(_hash_trajectory, jobs, chunksize=1)
    digest = hashlib.sha256()
    state_count = 0
    action_count = 0
    try:
        for trajectory_number, result in enumerate(results, start=1):
            digest.update(canonical_line(result))
            state_count += result["stateCount"]
            action_count += result["actionCount"]
            if trajectory_number % 100 == 0:
                print(
                    f"Каталог v{catalog_version}: обработано {trajectory_number} из {len(trajectories)} траекторий.",
                    file=sys.stderr,
                    flush=True,
                )
    finally:
        if executor is not None:
            executor.shutdown()
    return {
        "actionCount": action_count,
        "catalogVersion": catalog_version,
        "sha256": digest.hexdigest(),
        "stateCount": state_count,
    }


def catalog_report(dataset_root: Path, workers: int = 4) -> dict[str, Any]:
    """Строит сводку каталогов v1/v2 для указанного набора данных."""

    if workers <= 0:
        raise ValueError("число рабочих процессов должно быть положительным")
    manifest_path = dataset_root / "manifest.json"
    manifest = read_canonical_json(manifest_path)
    problems, trajectories = _load_records(dataset_root, manifest)
    return {
        "catalogs": [_hash_version(problems, trajectories, version, workers) for version in (1, 2)],
        "datasetManifestSha256": sha256_file(manifest_path),
        "format": "aipackaging.polygon_catalog_regression",
        "problemCount": len(problems),
        "trajectoryCount": len(trajectories),
        "version": 1,
    }


def main() -> int:
    """Печатает отчёт, при необходимости записывает его и сравнивает с эталоном."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, required=True, help="корень замороженного полигонального набора данных")
    parser.add_argument("--output", type=Path, help="необязательный путь канонического отчёта")
    parser.add_argument("--expected", type=Path, help="необязательный ранее зафиксированный отчёт")
    parser.add_argument("--workers", type=int, default=4, help="число параллельных рабочих процессов, по умолчанию 4")
    arguments = parser.parse_args()

    report = catalog_report(arguments.dataset.resolve(), arguments.workers)
    if arguments.expected and report != read_canonical_json(arguments.expected):
        print("Каталоги полигональных действий отличаются от эталона.", file=sys.stderr)
        return 1
    if arguments.output:
        write_canonical_json(arguments.output, report)
    print(json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
