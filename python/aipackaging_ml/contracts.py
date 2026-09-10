"""Строгие воспроизводимые контракты конфигурации и ML-артефактов M3."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Mapping


def sha256_file(path: str | Path) -> str:
    """Возвращает SHA-256 точного содержимого файла в нижнем регистре."""

    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def canonical_json(value: Mapping[str, Any]) -> str:
    """Кодирует объект стабильным UTF-8-совместимым JSON без лишних пробелов."""

    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def write_canonical_json(path: str | Path, value: Mapping[str, Any]) -> None:
    """Записывает канонический JSON с единственным завершающим переводом строки."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(canonical_json(value) + "\n", encoding="utf-8", newline="\n")


def _require_keys(value: Any, expected: set[str], label: str) -> Mapping[str, Any]:
    """Отклоняет не-объект, отсутствующие и неизвестные поля строгого контракта."""

    if not isinstance(value, Mapping) or set(value) != expected:
        actual = sorted(value) if isinstance(value, Mapping) else type(value).__name__
        raise ValueError(f"{label} fields mismatch: expected {sorted(expected)}, got {actual}")
    return value


def _positive_number(value: Any, label: str, *, allow_zero: bool = False) -> float:
    """Проверяет числовой гиперпараметр и возвращает его без изменения смысла."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a number")
    if value < 0 or (value == 0 and not allow_zero):
        raise ValueError(f"{label} must be {'non-negative' if allow_zero else 'positive'}")
    return float(value)


def _positive_int(value: Any, label: str) -> int:
    """Проверяет положительный целочисленный гиперпараметр."""

    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{label} must be a positive integer")
    return value


def validate_training_config(value: Mapping[str, Any]) -> dict[str, Any]:
    """Проверяет training_config v1 и возвращает независимую JSON-копию."""

    root = _require_keys(
        value,
        {"format", "version", "seed", "datasetManifestSha256", "model", "behavioralCloning", "ppo", "evaluation", "export"},
        "training config",
    )
    if root["format"] != "aipackaging.training_config" or root["version"] != 1:
        raise ValueError("unsupported training config format or version")
    if isinstance(root["seed"], bool) or not isinstance(root["seed"], int) or root["seed"] < 0:
        raise ValueError("seed must be a non-negative integer")
    dataset_hash = root["datasetManifestSha256"]
    if not isinstance(dataset_hash, str) or len(dataset_hash) != 64 or any(c not in "0123456789abcdef" for c in dataset_hash):
        raise ValueError("datasetManifestSha256 must be lowercase SHA-256")

    model = _require_keys(
        root["model"],
        {"architecture", "hiddenSize", "maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"},
        "model",
    )
    if model["architecture"] != "hierarchical-grid-policy-v1":
        raise ValueError("unsupported model architecture")
    for field in ("hiddenSize", "maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"):
        _positive_int(model[field], f"model.{field}")
    if model["hiddenSize"] < 16 or model["hiddenSize"] % 2 != 0:
        raise ValueError("model.hiddenSize must be an even integer of at least 16")

    bc = _require_keys(
        root["behavioralCloning"],
        {"learningRate", "weightDecay", "maxEpochs", "gradientAccumulation", "earlyStoppingPatience"},
        "behavioralCloning",
    )
    _positive_number(bc["learningRate"], "behavioralCloning.learningRate")
    _positive_number(bc["weightDecay"], "behavioralCloning.weightDecay", allow_zero=True)
    for field in ("maxEpochs", "gradientAccumulation", "earlyStoppingPatience"):
        _positive_int(bc[field], f"behavioralCloning.{field}")

    ppo = _require_keys(
        root["ppo"],
        {"learningRate", "updates", "transitionsPerUpdate", "epochsPerUpdate", "workers", "gamma", "gaeLambda", "clipRatio", "entropyStart", "entropyEnd", "valueCoefficient", "maxGradientNorm", "maxWallTimeSeconds"},
        "ppo",
    )
    for field in ("updates", "transitionsPerUpdate", "epochsPerUpdate", "workers", "maxWallTimeSeconds"):
        _positive_int(ppo[field], f"ppo.{field}")
    for field in ("learningRate", "clipRatio", "maxGradientNorm"):
        _positive_number(ppo[field], f"ppo.{field}")
    for field in ("gamma", "gaeLambda"):
        number = _positive_number(ppo[field], f"ppo.{field}", allow_zero=True)
        if number > 1:
            raise ValueError(f"ppo.{field} must be in [0, 1]")
    for field in ("entropyStart", "entropyEnd", "valueCoefficient"):
        _positive_number(ppo[field], f"ppo.{field}", allow_zero=True)

    evaluation = _require_keys(
        root["evaluation"],
        {"neuralRollouts", "baselineRandomIterations", "requiredLexicographicWins", "requiredUsedLengthWins"},
        "evaluation",
    )
    for field in evaluation:
        _positive_int(evaluation[field], f"evaluation.{field}")
    export = _require_keys(root["export"], {"opset", "logitTolerance"}, "export")
    _positive_int(export["opset"], "export.opset")
    _positive_number(export["logitTolerance"], "export.logitTolerance")
    return json.loads(canonical_json(root))


def load_training_config(path: str | Path) -> dict[str, Any]:
    """Загружает UTF-8 training_config v1 и применяет строгую проверку."""

    return validate_training_config(json.loads(Path(path).read_text(encoding="utf-8")))
