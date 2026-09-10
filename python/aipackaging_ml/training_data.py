"""Загрузка split и воспроизведение экспертных траекторий для M3."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator, Mapping

from .contracts import sha256_file
from .dataset import _read_jsonl_gzip
from .environment import GridNestingEnv


@dataclass(frozen=True)
class ExpertEpisode:
    """Связывает одну задачу с выбранной в manifest лучшей baseline-траекторией."""

    tier: str
    split: str
    problem: dict[str, Any]
    trajectory: dict[str, Any]


@dataclass(frozen=True)
class ExpertStep:
    """Содержит состояние до действия, целевой индекс и точный value target."""

    fixed: Mapping[str, Any]
    dynamic: Mapping[str, Any]
    action_index: int
    value_target: float


def load_expert_episodes(
    root: str | Path,
    split: str,
    *,
    expected_manifest_sha256: str | None = None,
    tiers: tuple[str, ...] = ("small", "medium"),
) -> list[ExpertEpisode]:
    """Загружает только expert-траектории заданного split в стабильном порядке."""

    if split not in {"train", "validation", "test"}:
        raise ValueError(f"unknown dataset split: {split}")
    dataset_root = Path(root)
    manifest_path = dataset_root / "manifest.json"
    actual_hash = sha256_file(manifest_path)
    if expected_manifest_sha256 is not None and actual_hash != expected_manifest_sha256.lower():
        raise ValueError(f"dataset manifest checksum mismatch: expected {expected_manifest_sha256}, got {actual_hash}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "aipackaging.grid_dataset" or manifest.get("version") != 1:
        raise ValueError("unsupported grid_dataset manifest")

    problems: dict[str, tuple[str, dict[str, Any]]] = {}
    trajectories: dict[str, dict[str, Any]] = {}
    for shard in manifest["shards"]:
        if shard["split"] != split or shard["tier"] not in tiers:
            continue
        records = _read_jsonl_gzip(dataset_root / shard["path"])
        if shard["kind"] == "problems":
            for problem in records:
                problems[problem["problemId"]] = (shard["tier"], problem)
        elif shard["kind"] == "trajectories":
            for trajectory in records:
                trajectories[trajectory["trajectoryId"]] = trajectory

    episodes = []
    for problem_id, (tier, problem) in sorted(problems.items()):
        trajectory_id = manifest["expertTrajectoryId"].get(problem_id)
        if trajectory_id not in trajectories:
            raise ValueError(f"expert trajectory is absent from split: {problem_id}")
        trajectory = trajectories[trajectory_id]
        if trajectory["problemId"] != problem_id:
            raise ValueError(f"expert trajectory references another problem: {trajectory_id}")
        episodes.append(ExpertEpisode(tier, split, problem, trajectory))
    return episodes


def load_frozen_baseline_solutions(
    root: str | Path,
    split: str,
    *,
    expected_manifest_sha256: str | None = None,
    tiers: tuple[str, ...] = ("small", "medium"),
) -> dict[str, dict[str, dict[str, Any]]]:
    """Загружает сохранённые M2-решения всех пяти baseline по problemId и имени."""

    dataset_root = Path(root)
    manifest_path = dataset_root / "manifest.json"
    if expected_manifest_sha256 is not None and sha256_file(manifest_path) != expected_manifest_sha256.lower():
        raise ValueError("dataset manifest checksum mismatch")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    result: dict[str, dict[str, dict[str, Any]]] = {}
    for shard in manifest["shards"]:
        if shard["split"] != split or shard["tier"] not in tiers or shard["kind"] != "trajectories":
            continue
        for trajectory in _read_jsonl_gzip(dataset_root / shard["path"]):
            result.setdefault(trajectory["problemId"], {})[trajectory["solver"]["name"]] = trajectory["finalSolution"]
    return result


def replay_expert_steps(episode: ExpertEpisode) -> Iterator[ExpertStep]:
    """Воспроизводит состояния до expert-действий без сохранения observations в датасете."""

    environment = GridNestingEnv.from_dict(episode.problem)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    steps = episode.trajectory["steps"]
    final_rank = steps[-1]["rankAfter"] if steps else environment.rank
    for step in steps:
        # Телескопическая reward v1 делает оставшийся return точной разностью
        # финального и текущего смешанного ранга.
        value_target = (final_rank - environment.rank) / environment.rank_upper_bound
        yield ExpertStep(fixed, dynamic, int(step["actionIndex"]), value_target)
        dynamic, reward, terminated, truncated, info = environment.step_compact(int(step["actionIndex"]))
        if truncated or abs(reward - step["reward"]) > 1e-15 or info["rankAfter"] != step["rankAfter"]:
            raise ValueError(f"expert replay mismatch: {episode.trajectory['trajectoryId']}")
        if terminated != bool(step["terminated"]):
            raise ValueError(f"expert termination mismatch: {episode.trajectory['trajectoryId']}")
