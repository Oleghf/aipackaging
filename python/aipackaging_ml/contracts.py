"""Строгие воспроизводимые контракты конфигурации и ML-артефактов M3."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from .datasets.serialization import canonical_json, require_keys as _require_keys, sha256_file, write_canonical_json


def _positive_number(value: Any, label: str, *, allow_zero: bool = False) -> float:
    """Проверяет числовой гиперпараметр и возвращает его без изменения смысла."""

    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label}: ожидалось число")
    if value < 0 or (value == 0 and not allow_zero):
        requirement = "неотрицательное значение" if allow_zero else "положительное значение"
        raise ValueError(f"{label}: ожидалось {requirement}")
    return float(value)


def _positive_int(value: Any, label: str) -> int:
    """Проверяет положительный целочисленный гиперпараметр."""

    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{label}: ожидалось положительное целое число")
    return value


def validate_training_config(value: Mapping[str, Any]) -> dict[str, Any]:
    """Проверяет `training_config` v1 и возвращает независимую копию JSON."""

    root = _require_keys(
        value,
        {"format", "version", "seed", "datasetManifestSha256", "model", "behavioralCloning", "ppo", "evaluation", "export"},
        "конфигурация обучения",
    )
    if root["format"] != "aipackaging.training_config" or root["version"] != 1:
        raise ValueError("неподдерживаемый формат или версия конфигурации обучения")
    if isinstance(root["seed"], bool) or not isinstance(root["seed"], int) or root["seed"] < 0:
        raise ValueError("поле `seed`: ожидалось неотрицательное целое число")
    dataset_hash = root["datasetManifestSha256"]
    if not isinstance(dataset_hash, str) or len(dataset_hash) != 64 or any(c not in "0123456789abcdef" for c in dataset_hash):
        raise ValueError("datasetManifestSha256: ожидался SHA-256 в нижнем регистре")

    model = _require_keys(
        root["model"],
        {"architecture", "hiddenSize", "maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"},
        "модель",
    )
    if model["architecture"] != "hierarchical-grid-policy-v1":
        raise ValueError("неподдерживаемая архитектура модели")
    for field in ("hiddenSize", "maxRows", "maxColumns", "maxInstances", "maxPartExtent", "maxActions"):
        _positive_int(model[field], f"model.{field}")
    if model["hiddenSize"] < 16 or model["hiddenSize"] % 2 != 0:
        raise ValueError("model.hiddenSize: ожидалось чётное целое число не меньше 16")

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
            raise ValueError(f"ppo.{field}: значение должно находиться в диапазоне [0, 1]")
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
    """Загружает `training_config` v1 в UTF-8 и применяет строгую проверку."""

    return validate_training_config(json.loads(Path(path).read_text(encoding="utf-8")))
