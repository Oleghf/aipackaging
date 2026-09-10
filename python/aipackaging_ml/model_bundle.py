"""Строгая проверка метаданных и численной совместимости ONNX bundle."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

import numpy as np

from .contracts import sha256_file


def bundle_digest(files: Mapping[str, str]) -> str:
    """Получает общий model hash из стабильной карты имён и SHA-256 частей."""

    payload = json.dumps(dict(sorted(files.items())), sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def load_model_metadata(path: str | Path) -> dict[str, Any]:
    """Строго загружает metadata и проверяет hashes и opset обеих ONNX-частей."""

    root = Path(path)
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    expected = {
        "format", "version", "modelId", "projectVersion", "architecture", "opset", "contracts", "limits",
        "normalization", "selection", "dynamicAxes", "datasetManifestSha256", "trainingConfigSha256",
        "checkpointSha256", "files", "modelSha256",
    }
    if set(metadata) != expected or metadata["format"] != "aipackaging.grid_policy" or metadata["version"] != 1:
        raise ValueError("unsupported or non-strict grid_policy metadata")
    if metadata["architecture"] != "hierarchical-grid-policy-v1" or metadata["opset"] != 23:
        raise ValueError("model architecture or opset is incompatible")
    if set(metadata["contracts"]) != {"problem", "solution", "observation", "action", "reward"} or metadata[
        "contracts"
    ] != {"problem": 1, "solution": 2, "observation": 1, "action": 1, "reward": 1}:
        raise ValueError("model contract versions are incompatible")
    if set(metadata["limits"]) != {"maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"}:
        raise ValueError("model limits fields mismatch")
    if set(metadata["selection"]) != {"hierarchy", "tieBreak"} or metadata["selection"] != {
        "hierarchy": ["instance", "rotation", "position"],
        "tieBreak": "lowest-stable-action-index",
    }:
        raise ValueError("model selection contract is incompatible")
    expected_axes = ["rows", "columns", "instances", "part_rows", "part_columns", "candidates"]
    if metadata["dynamicAxes"] != expected_axes or metadata["normalization"] != "observation-v1":
        raise ValueError("model observation contract is incompatible")
    if set(metadata["files"]) != {"encoder.onnx", "placement-head.onnx"}:
        raise ValueError("model bundle file set mismatch")
    hashes = [
        metadata["datasetManifestSha256"],
        metadata["trainingConfigSha256"],
        metadata["checkpointSha256"],
        metadata["modelSha256"],
        *metadata["files"].values(),
    ]
    if any(not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value) for value in hashes):
        raise ValueError("model metadata contains an invalid SHA-256")
    actual = {name: sha256_file(root / name) for name in sorted(metadata["files"])}
    if actual != metadata["files"] or bundle_digest(actual) != metadata["modelSha256"]:
        raise ValueError("model bundle checksum mismatch")

    import onnx

    # Metadata не должна заявлять более новый контракт, чем реально содержат
    # графы: hashes защищают байты, а эта проверка защищает их интерпретацию.
    for name in sorted(metadata["files"]):
        model = onnx.load(root / name, load_external_data=False)
        default_opset = next((item.version for item in model.opset_import if not item.domain), None)
        if default_opset != metadata["opset"]:
            raise ValueError(f"ONNX opset mismatch in {name}")
    return metadata


def compare_arrays(expected: np.ndarray, actual: np.ndarray, tolerance: float) -> None:
    """Отклоняет различие формы либо чисел PyTorch/ONNX выше допуска."""

    if expected.shape != actual.shape or not np.allclose(expected, actual, atol=tolerance, rtol=0.0):
        maximum = float(np.max(np.abs(expected - actual))) if expected.shape == actual.shape and expected.size else float("inf")
        raise ValueError(f"ONNX parity mismatch: shape {expected.shape}/{actual.shape}, max error {maximum}")
