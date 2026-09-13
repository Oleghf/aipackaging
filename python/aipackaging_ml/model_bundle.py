"""Строгая проверка метаданных и численной совместимости комплекта ONNX."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

import numpy as np

from .contracts import sha256_file


def bundle_digest(files: Mapping[str, str]) -> str:
    """Получает общий хеш модели из стабильной карты имён и SHA-256 частей."""

    payload = json.dumps(dict(sorted(files.items())), sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def load_model_metadata(path: str | Path) -> dict[str, Any]:
    """Строго загружает метаданные и проверяет хеши и наборы операций частей ONNX."""

    root = Path(path)
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    expected = {
        "format", "version", "modelId", "projectVersion", "architecture", "opset", "contracts", "limits",
        "normalization", "selection", "dynamicAxes", "datasetManifestSha256", "trainingConfigSha256",
        "checkpointSha256", "files", "modelSha256",
    }
    if set(metadata) != expected or metadata["format"] != "aipackaging.grid_policy" or metadata["version"] != 1:
        raise ValueError("неподдерживаемые или нестрогие метаданные `grid_policy`")
    if metadata["architecture"] != "hierarchical-grid-policy-v1" or metadata["opset"] != 23:
        raise ValueError("архитектура модели или версия набора операций несовместима")
    if set(metadata["contracts"]) != {"problem", "solution", "observation", "action", "reward"} or metadata[
        "contracts"
    ] != {"problem": 1, "solution": 2, "observation": 1, "action": 1, "reward": 1}:
        raise ValueError("версии контрактов модели несовместимы")
    if set(metadata["limits"]) != {"maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"}:
        raise ValueError("набор полей ограничений модели не совпадает")
    if set(metadata["selection"]) != {"hierarchy", "tieBreak"} or metadata["selection"] != {
        "hierarchy": ["instance", "rotation", "position"],
        "tieBreak": "lowest-stable-action-index",
    }:
        raise ValueError("контракт выбора модели несовместим")
    expected_axes = ["rows", "columns", "instances", "part_rows", "part_columns", "candidates"]
    if metadata["dynamicAxes"] != expected_axes or metadata["normalization"] != "observation-v1":
        raise ValueError("контракт наблюдения модели несовместим")
    if set(metadata["files"]) != {"encoder.onnx", "placement-head.onnx"}:
        raise ValueError("набор файлов комплекта модели не совпадает")
    hashes = [
        metadata["datasetManifestSha256"],
        metadata["trainingConfigSha256"],
        metadata["checkpointSha256"],
        metadata["modelSha256"],
        *metadata["files"].values(),
    ]
    if any(not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value) for value in hashes):
        raise ValueError("метаданные модели содержат некорректный SHA-256")
    actual = {name: sha256_file(root / name) for name in sorted(metadata["files"])}
    if actual != metadata["files"] or bundle_digest(actual) != metadata["modelSha256"]:
        raise ValueError("контрольная сумма комплекта модели не совпадает")

    import onnx

    # Метаданные не должны заявлять более новый контракт, чем реально содержат
    # графы: хеши защищают байты, а эта проверка защищает их интерпретацию.
    for name in sorted(metadata["files"]):
        model = onnx.load(root / name, load_external_data=False)
        default_opset = next((item.version for item in model.opset_import if not item.domain), None)
        if default_opset != metadata["opset"]:
            raise ValueError(f"версия набора операций ONNX не совпадает в {name}")
    return metadata


def compare_arrays(expected: np.ndarray, actual: np.ndarray, tolerance: float) -> None:
    """Отклоняет различие формы либо чисел PyTorch/ONNX выше допуска."""

    if expected.shape != actual.shape or not np.allclose(expected, actual, atol=tolerance, rtol=0.0):
        maximum = float(np.max(np.abs(expected - actual))) if expected.shape == actual.shape and expected.size else float("inf")
        raise ValueError(f"вычисления ONNX не совпадают: формы {expected.shape}/{actual.shape}, ошибка {maximum}")
