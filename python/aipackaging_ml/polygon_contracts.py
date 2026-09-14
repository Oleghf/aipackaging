"""Строгие контракты обучения и оценки полигональной политики."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from .datasets.serialization import canonical_json, require_keys


def _positive_int(value: Any, label: str) -> int:
    """Проверяет положительный целочисленный параметр."""

    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise ValueError(f"{label}: ожидалось положительное целое число")
    return value


def _number(value: Any, label: str, *, zero: bool = False) -> float:
    """Проверяет положительный либо неотрицательный числовой параметр."""

    if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0 or (value == 0 and not zero):
        raise ValueError(f"{label}: ожидалось {'неотрицательное' if zero else 'положительное'} число")
    return float(value)


def validate_polygon_training_config(value: Mapping[str, Any]) -> dict[str, Any]:
    """Строго проверяет `polygon_training_config` v1."""

    root = require_keys(value, {"format", "version", "seed", "datasetManifestSha256", "rewardVersion", "model",
                                "behavioralCloning", "ppo", "evaluation"}, "конфигурация полигонального обучения")
    if root["format"] != "aipackaging.polygon_training_config" or root["version"] != 1:
        raise ValueError("неподдерживаемая конфигурация полигонального обучения")
    if isinstance(root["seed"], bool) or not isinstance(root["seed"], int) or root["seed"] < 0:
        raise ValueError("`seed` должен быть неотрицательным целым числом")
    digest = root["datasetManifestSha256"]
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise ValueError("`datasetManifestSha256` должен быть SHA-256 в нижнем регистре")
    if root["rewardVersion"] != 2:
        raise ValueError("полигональное обучение M6.2 требует вознаграждение v2")
    model = require_keys(root["model"], {"architecture", "hiddenSize", "sheetRasterSize", "shapeRasterSize",
                                                "maxInstances", "maxCandidatesPerPair"}, "модель")
    if model["architecture"] != "hierarchical-polygon-policy-v1":
        raise ValueError("неподдерживаемая архитектура полигональной модели")
    for field in ("hiddenSize", "sheetRasterSize", "shapeRasterSize", "maxInstances", "maxCandidatesPerPair"):
        _positive_int(model[field], f"model.{field}")
    if model["hiddenSize"] < 16 or model["hiddenSize"] % 2 or model["sheetRasterSize"] != 128 or model["shapeRasterSize"] != 32:
        raise ValueError("размеры полигональной модели не соответствуют контракту наблюдения v2")
    bc = require_keys(root["behavioralCloning"], {"learningRate", "weightDecay", "maxEpochs", "gradientAccumulation",
                                                       "earlyStoppingPatience"}, "behavioralCloning")
    _number(bc["learningRate"], "behavioralCloning.learningRate")
    _number(bc["weightDecay"], "behavioralCloning.weightDecay", zero=True)
    for field in ("maxEpochs", "gradientAccumulation", "earlyStoppingPatience"):
        _positive_int(bc[field], f"behavioralCloning.{field}")
    ppo = require_keys(root["ppo"], {"learningRate", "updates", "transitionsPerUpdate", "epochsPerUpdate", "workers",
                                           "gamma", "gaeLambda", "clipRatio", "entropyStart", "entropyEnd",
                                           "valueCoefficient", "maxGradientNorm", "maxWallTimeSeconds"}, "ppo")
    for field in ("updates", "transitionsPerUpdate", "epochsPerUpdate", "workers", "maxWallTimeSeconds"):
        _positive_int(ppo[field], f"ppo.{field}")
    for field in ("learningRate", "clipRatio", "maxGradientNorm"):
        _number(ppo[field], f"ppo.{field}")
    for field in ("gamma", "gaeLambda", "entropyStart", "entropyEnd", "valueCoefficient"):
        _number(ppo[field], f"ppo.{field}", zero=True)
    if ppo["gamma"] > 1 or ppo["gaeLambda"] > 1 or ppo["clipRatio"] > 1:
        raise ValueError("параметры PPO находятся вне допустимого диапазона")
    evaluation = require_keys(root["evaluation"], {"neuralRollouts", "requiredLexicographicWins", "requiredUsedLengthWins"},
                              "evaluation")
    for field in evaluation:
        _positive_int(evaluation[field], f"evaluation.{field}")
    return json.loads(canonical_json(root))


def load_polygon_training_config(path: str | Path) -> dict[str, Any]:
    """Загружает и проверяет конфигурацию полигонального обучения."""

    return validate_polygon_training_config(json.loads(Path(path).read_text(encoding="utf-8")))
