"""Внутренний проверяемый кэш наблюдений полигонального BC."""

from __future__ import annotations

import io
import json
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np

from .datasets.serialization import canonical_json
from .polygon_training_data import PolygonExpertEpisode, replay_polygon_expert_steps


@dataclass(frozen=True)
class CachedPolygonExpertStep:
    """Содержит достаточные данные для обучения без повторной геометрии."""

    fixed: Mapping[str, Any]
    dynamic: Mapping[str, Any]
    placement: Mapping[str, Any]
    instance: int
    rotation: int
    position: int
    value_target: float


def _array_bytes(value: np.ndarray) -> bytes:
    """Кодирует массив NumPy без объектов и исполняемого содержимого."""

    stream = io.BytesIO()
    np.save(stream, np.asarray(value), allow_pickle=False)
    return stream.getvalue()


def _read_array(archive: zipfile.ZipFile, name: str) -> np.ndarray:
    """Читает один безопасный массив из архива только для чтения."""

    value = np.load(io.BytesIO(archive.read(name)), allow_pickle=False)
    value.setflags(write=False)
    return value


def _target_indices(sample: Any) -> tuple[int, int, int, Mapping[str, Any]]:
    """Преобразует аудируемое действие в три уровня и условное наблюдение."""

    target = sample.environment.action(sample.action_index)
    instance = next(index for index, pair in enumerate(zip(sample.fixed["instance_part_ids"],
                                                           sample.fixed["instance_indices"], strict=True))
                    if pair[0] == target["partId"] and int(pair[1]) == target["instanceIndex"])
    rotation = int(target["rotationDegrees"]) // 90
    placement = sample.environment.placement_observation(instance, rotation * 90)
    position = next(index for index, action in enumerate(placement["actions"]) if action == target)
    return instance, rotation, position, placement


def write_polygon_observation_cache(path: str | Path, episodes: Mapping[str, Sequence[PolygonExpertEpisode]],
                                    fingerprint: str) -> dict[str, list[CachedPolygonExpertStep]]:
    """Строит архив наблюдений атомарно и возвращает подготовленные шаги."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    metadata: dict[str, Any] = {"format": "aipackaging.polygon_observation_cache", "version": 1,
                                "fingerprint": fingerprint, "splits": {}}
    result: dict[str, list[CachedPolygonExpertStep]] = {}
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for split, split_episodes in episodes.items():
            output: list[CachedPolygonExpertStep] = []
            records = []
            for episode_index, episode in enumerate(split_episodes):
                fixed: Mapping[str, Any] | None = None
                for step_index, sample in enumerate(replay_polygon_expert_steps(episode)):
                    instance, rotation, position, placement = _target_indices(sample)
                    fixed = sample.fixed
                    prefix = f"{split}/{episode_index:04d}/{step_index:04d}"
                    arrays = {
                        "part_masks": np.asarray(sample.fixed["part_masks"]),
                        "part_features": np.asarray(sample.fixed["part_features"]),
                        "occupied": np.asarray(sample.dynamic["occupied"]),
                        "clearance": np.asarray(sample.dynamic["clearance"]),
                        "pair_mask": np.asarray(sample.dynamic["pair_mask"]),
                        "objective": np.asarray(sample.dynamic["objective"]),
                        "raster": np.asarray(placement["raster"]),
                        "candidate_features": np.asarray(placement["candidate_features"]),
                    }
                    for name, value in arrays.items():
                        archive.writestr(f"{prefix}/{name}.npy", _array_bytes(value))
                    records.append({"prefix": prefix, "instance": instance, "rotation": rotation,
                                    "position": position, "valueTarget": sample.value_target})
                    output.append(CachedPolygonExpertStep(
                        {"part_masks": arrays["part_masks"], "part_features": arrays["part_features"]},
                        {"occupied": arrays["occupied"], "clearance": arrays["clearance"],
                         "pair_mask": arrays["pair_mask"], "objective": arrays["objective"]},
                        {"raster": arrays["raster"], "candidate_features": arrays["candidate_features"]},
                        instance, rotation, position, sample.value_target,
                    ))
                if fixed is None:
                    raise ValueError(f"экспертный эпизод не содержит действий: {episode.problem['problemId']}")
            metadata["splits"][split] = records
            result[split] = output
        archive.writestr("metadata.json", canonical_json(metadata).encode("utf-8"))
    temporary.replace(destination)
    return result


def read_polygon_observation_cache(path: str | Path, fingerprint: str) -> dict[str, list[CachedPolygonExpertStep]]:
    """Читает только совместимый полный кэш и отклоняет повреждение."""

    source = Path(path)
    with zipfile.ZipFile(source, "r") as archive:
        metadata = json.loads(archive.read("metadata.json").decode("utf-8"))
        if set(metadata) != {"format", "version", "fingerprint", "splits"} or metadata["format"] != "aipackaging.polygon_observation_cache" or metadata["version"] != 1:
            raise ValueError("неподдерживаемый кэш полигональных наблюдений")
        if metadata["fingerprint"] != fingerprint:
            raise ValueError("кэш полигональных наблюдений относится к другой конфигурации")
        result: dict[str, list[CachedPolygonExpertStep]] = {}
        for split, records in metadata["splits"].items():
            output = []
            for record in records:
                prefix = record["prefix"]
                fixed = {name: _read_array(archive, f"{prefix}/{name}.npy") for name in ("part_masks", "part_features")}
                dynamic = {name: _read_array(archive, f"{prefix}/{name}.npy") for name in ("occupied", "clearance", "pair_mask", "objective")}
                placement = {name: _read_array(archive, f"{prefix}/{name}.npy") for name in ("raster", "candidate_features")}
                output.append(CachedPolygonExpertStep(fixed, dynamic, placement, int(record["instance"]),
                                                     int(record["rotation"]), int(record["position"]),
                                                     float(record["valueTarget"])))
            result[split] = output
        return result


def load_or_build_polygon_observation_cache(path: str | Path, episodes: Mapping[str, Sequence[PolygonExpertEpisode]],
                                            fingerprint: str) -> dict[str, list[CachedPolygonExpertStep]]:
    """Использует совместимый кэш либо полностью и атомарно перестраивает его."""

    source = Path(path)
    if source.exists():
        try:
            return read_polygon_observation_cache(source, fingerprint)
        except (KeyError, OSError, ValueError, zipfile.BadZipFile):
            pass
    return write_polygon_observation_cache(source, episodes, fingerprint)
