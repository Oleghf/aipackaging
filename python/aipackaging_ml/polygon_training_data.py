"""Загрузка и повторное проигрывание экспертных полигональных траекторий."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator, Mapping

from .datasets.serialization import read_jsonl_gzip, sha256_file
from .environment import PolygonNestingEnv


@dataclass(frozen=True)
class PolygonExpertEpisode:
    """Связывает задачу с выбранной экспертной траекторией M6.1."""

    tier: str
    split: str
    problem: dict[str, Any]
    trajectory: dict[str, Any]


@dataclass(frozen=True)
class PolygonExpertStep:
    """Предоставляет живую среду, наблюдение, действие и целевую отдачу."""

    environment: PolygonNestingEnv
    fixed: Mapping[str, Any]
    dynamic: Mapping[str, Any]
    action_index: int
    value_target: float


def _dataset_records(root: Path, manifest: Mapping[str, Any], split: str, kind: str) -> list[dict[str, Any]]:
    """Читает части набора требуемой выборки и вида в порядке манифеста."""

    records: list[dict[str, Any]] = []
    for shard in manifest["shards"]:
        if shard["split"] == split and shard["kind"] == kind:
            records.extend(read_jsonl_gzip(root / shard["path"]))
    return records


def load_polygon_expert_episodes(root: str | Path, split: str, *, expected_manifest_sha256: str) -> list[PolygonExpertEpisode]:
    """Загружает экспертные эпизоды выбранной полигональной выборки."""

    if split not in {"train", "validation", "test"}:
        raise ValueError(f"неизвестная выборка набора данных: {split}")
    dataset_root = Path(root)
    manifest_path = dataset_root / "manifest.json"
    actual_hash = sha256_file(manifest_path)
    if actual_hash != expected_manifest_sha256.lower():
        raise ValueError(f"контрольная сумма манифеста не совпадает: ожидалась {expected_manifest_sha256}, получена {actual_hash}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "aipackaging.polygon_dataset" or manifest.get("version") != 2:
        raise ValueError("для обучения требуется `polygon_dataset` v2")
    problems = {item["problemId"]: item for item in _dataset_records(dataset_root, manifest, split, "problems")}
    trajectories = {item["trajectoryId"]: item for item in _dataset_records(dataset_root, manifest, split, "trajectories")}
    episodes = []
    for problem_id in sorted(problems):
        trajectory_id = manifest["expertTrajectoryId"].get(problem_id)
        if trajectory_id not in trajectories:
            raise ValueError(f"экспертная траектория отсутствует: {problem_id}")
        metadata = manifest["problems"][problem_id]
        episodes.append(PolygonExpertEpisode(metadata["tier"], split, problems[problem_id], trajectories[trajectory_id]))
    return episodes


def replay_polygon_expert_steps(episode: PolygonExpertEpisode) -> Iterator[PolygonExpertStep]:
    """Воспроизводит экспертные состояния с отдачей нового вознаграждения v2."""

    actions = [step["action"] for step in episode.trajectory["steps"]]
    rewards: list[float] = []
    probe = PolygonNestingEnv.from_dict(episode.problem, reward_version=2, catalog_version=1)
    probe.reset_compact()
    for action in actions:
        _, reward, _, truncated, _ = probe.step_compact(probe.find_action(action))
        if truncated:
            raise ValueError(f"экспертная траектория неожиданно усечена: {episode.trajectory['trajectoryId']}")
        rewards.append(float(reward))
    returns = [0.0] * len(rewards)
    accumulated = 0.0
    for index in range(len(rewards) - 1, -1, -1):
        accumulated += rewards[index]
        returns[index] = accumulated

    environment = PolygonNestingEnv.from_dict(episode.problem, reward_version=2, catalog_version=1)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    for action, value_target in zip(actions, returns, strict=True):
        action_index = environment.find_action(action)
        yield PolygonExpertStep(environment, fixed, dynamic, action_index, value_target)
        dynamic, _, _, _, _ = environment.step_compact(action_index)


def load_polygon_baseline_solutions(root: str | Path, split: str, *, expected_manifest_sha256: str) -> dict[str, dict[str, dict[str, Any]]]:
    """Загружает замороженные решения базовых алгоритмов для итогового сравнения."""

    episodes = load_polygon_expert_episodes(root, split, expected_manifest_sha256=expected_manifest_sha256)
    dataset_root = Path(root)
    manifest = json.loads((dataset_root / "manifest.json").read_text(encoding="utf-8"))
    result: dict[str, dict[str, dict[str, Any]]] = {episode.problem["problemId"]: {} for episode in episodes}
    for trajectory in _dataset_records(dataset_root, manifest, split, "trajectories"):
        result[trajectory["problemId"]][trajectory["solver"]["name"]] = trajectory["finalSolution"]
    return result
