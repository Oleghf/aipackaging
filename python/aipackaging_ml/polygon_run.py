"""Фиксация остановленного запуска полигонального обучения."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Mapping

import torch

from .datasets.serialization import canonical_json, read_canonical_json, require_keys, sha256_file, write_canonical_json
from .polygon_contracts import load_polygon_training_config
from .polygon_model import HierarchicalPolygonPolicyV1
from .polygon_training import load_polygon_checkpoint


def _checkpoint_descriptor(path: Path) -> dict[str, str]:
    """Проверяет контрольную точку и возвращает её переносимый описатель."""

    digest = sha256_file(path)
    sidecar = path.with_suffix(path.suffix + ".sha256")
    if not sidecar.is_file() or sidecar.read_text(encoding="ascii").strip() != digest:
        raise ValueError(f"файл SHA-256 контрольной точки не совпадает: {path.name}")
    return {"path": path.name, "sha256": digest}


def _load_checkpoint(path: Path, config: Mapping[str, Any]) -> tuple[dict[str, Any], dict[str, str]]:
    """Загружает контрольную точку на CPU и сверяет конфигурацию обучения."""

    descriptor = _checkpoint_descriptor(path)
    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"])
    payload = load_polygon_checkpoint(path, model, torch.device("cpu"), expected_sha256=descriptor["sha256"])
    if canonical_json(payload["config"]) != canonical_json(config):
        raise ValueError(f"конфигурация контрольной точки не совпадает: {path.name}")
    return payload, descriptor


def _policy(config: Mapping[str, Any], config_hash: str, checkpoint_hash: str) -> dict[str, Any]:
    """Формирует метаданные выбранной полигональной политики."""

    return {
        "format": "aipackaging.polygon_policy",
        "version": 1,
        "modelId": "hierarchical-polygon-policy-v1",
        "architecture": config["model"]["architecture"],
        "observationVersion": 2,
        "rewardVersion": config["rewardVersion"],
        "datasetManifestSha256": config["datasetManifestSha256"],
        "configSha256": config_hash,
        "checkpointSha256": checkpoint_hash,
        "limits": dict(config["model"]),
        "actionSelection": {
            "levels": ["instance", "rotation", "position"],
            "tieBreak": "lowest-current-catalog-index",
        },
    }


def finalize_polygon_run(config_path: str | Path, dataset_root: str | Path, run_dir: str | Path, *,
                         elapsed_training_seconds: float | None = None, device_name: str = "cuda") -> dict[str, Any]:
    """Фиксирует остановленный по общему бюджету запуск без изменения весов."""

    config_source = Path(config_path)
    dataset_path = Path(dataset_root)
    root = Path(run_dir)
    config = load_polygon_training_config(config_source)
    limit = float(config["ppo"]["maxWallTimeSeconds"])
    elapsed = limit if elapsed_training_seconds is None else float(elapsed_training_seconds)
    if not (0.0 <= elapsed <= limit):
        raise ValueError("накопленное время должно находиться внутри общего бюджета обучения")
    if elapsed < limit:
        raise ValueError("запуск можно завершить как исчерпавший бюджет только после достижения общего предела")

    manifest_path = dataset_path / "manifest.json"
    if sha256_file(manifest_path) != config["datasetManifestSha256"]:
        raise ValueError("SHA-256 манифеста набора данных не совпадает с конфигурацией")
    bc_payload, bc_descriptor = _load_checkpoint(root / "polygon-bc-best.pt", config)
    selected_payload, selected_descriptor = _load_checkpoint(root / "polygon-ppo-best.pt", config)
    latest_payload, latest_descriptor = _load_checkpoint(root / "polygon-resume.pt", config)
    if selected_payload["stage"] != "ppo" or latest_payload["stage"] != "ppo":
        raise ValueError("для итоговой фиксации требуются контрольные точки этапа PPO")
    if selected_payload["step"] > latest_payload["step"]:
        raise ValueError("выбранная контрольная точка не может быть новее последней")

    config_hash = sha256_file(config_source)
    policy_path = root / "polygon-policy.json"
    write_canonical_json(policy_path, _policy(config, config_hash, selected_descriptor["sha256"]))

    summary = {
        "format": "aipackaging.polygon_training_summary",
        "version": 1,
        "status": "budget_exhausted",
        "terminationReason": "total_training_budget_reached",
        "elapsedTrainingSeconds": elapsed,
        "trainingHistoryComplete": False,
        "selectedCheckpoint": {
            **selected_descriptor,
            "stage": selected_payload["stage"],
            "step": selected_payload["step"],
            "validationScore": selected_payload["trainingState"].get("bestScore"),
        },
        "latestCheckpoint": {
            **latest_descriptor,
            "stage": latest_payload["stage"],
            "step": latest_payload["step"],
            "validationScore": latest_payload["trainingState"].get("bestScore"),
        },
    }
    summary_path = root / "training-summary.json"
    write_canonical_json(summary_path, summary)

    cache_path = root / "polygon-observation-cache.zip"
    if not cache_path.is_file():
        raise ValueError("отсутствует кэш наблюдений полигонального обучения")
    manifest = {
        "format": "aipackaging.polygon_training_run",
        "version": 1,
        "status": "budget_exhausted",
        "seed": config["seed"],
        "device": device_name,
        "datasetManifestSha256": config["datasetManifestSha256"],
        "configSha256": config_hash,
        "bcCheckpoint": bc_descriptor,
        "ppoCheckpoint": selected_descriptor,
        "policy": {"path": policy_path.name, "sha256": sha256_file(policy_path)},
        "metrics": {"path": summary_path.name, "sha256": sha256_file(summary_path)},
        "observationCache": {"path": cache_path.name, "sha256": sha256_file(cache_path)},
        "software": {
            "python": os.sys.version.split()[0],
            "torch": torch.__version__,
            "cuda": torch.version.cuda or "none",
        },
    }
    write_canonical_json(root / "run-manifest.json", manifest)
    return manifest


def validate_polygon_training_summary(value: Mapping[str, Any]) -> dict[str, Any]:
    """Строго проверяет сводку остановленного полигонального обучения."""

    summary = require_keys(
        value,
        {
            "format", "version", "status", "terminationReason", "elapsedTrainingSeconds",
            "trainingHistoryComplete", "selectedCheckpoint", "latestCheckpoint",
        },
        "сводка полигонального обучения",
    )
    if summary["format"] != "aipackaging.polygon_training_summary" or summary["version"] != 1:
        raise ValueError("неподдерживаемая сводка полигонального обучения")
    if summary["status"] != "budget_exhausted" or summary["terminationReason"] != "total_training_budget_reached":
        raise ValueError("некорректная причина завершения полигонального обучения")
    if summary["trainingHistoryComplete"] is not False:
        raise ValueError("сводка внешне остановленного запуска не должна объявлять полную историю")
    elapsed = summary["elapsedTrainingSeconds"]
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or elapsed < 0:
        raise ValueError("сводка содержит некорректное накопленное время")
    for field in ("selectedCheckpoint", "latestCheckpoint"):
        checkpoint = require_keys(
            summary[field], {"path", "sha256", "stage", "step", "validationScore"}, field
        )
        if Path(checkpoint["path"]).name != checkpoint["path"] or checkpoint["stage"] != "ppo":
            raise ValueError(f"сводка содержит некорректную контрольную точку: {field}")
        if not isinstance(checkpoint["step"], int) or checkpoint["step"] < 0:
            raise ValueError(f"сводка содержит некорректный шаг: {field}")
    if summary["selectedCheckpoint"]["step"] > summary["latestCheckpoint"]["step"]:
        raise ValueError("выбранная контрольная точка не может быть новее последней")
    return dict(summary)


def read_polygon_training_summary(path: str | Path) -> dict[str, Any]:
    """Читает и строго проверяет каноническую сводку обучения."""

    return validate_polygon_training_summary(read_canonical_json(path))
