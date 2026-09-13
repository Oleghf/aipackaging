"""Тесты строгих контрактов машинного обучения без нативной привязки."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from aipackaging_ml.contracts import load_training_config, validate_training_config

CONFIG = Path(__file__).parents[2] / "configs" / "m3" / "grid-policy-v1.json"


def test_canonical_training_config_is_strict() -> None:
    """Каноническая конфигурация принимается, а неизвестное поле отклоняется."""

    config = load_training_config(CONFIG)
    assert config["seed"] == 42
    assert config["ppo"]["updates"] == 122
    config["unknown"] = True
    with pytest.raises(ValueError, match="ожидались поля"):
        validate_training_config(config)


def test_training_config_rejects_invalid_ranges_and_hash() -> None:
    """Неверный бюджет и контрольная сумма не проходят ручную проверку схемы."""

    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    config["ppo"]["gamma"] = 1.1
    with pytest.raises(ValueError, match="gamma"):
        validate_training_config(config)
    config = json.loads(CONFIG.read_text(encoding="utf-8"))
    config["datasetManifestSha256"] = "bad"
    with pytest.raises(ValueError, match="SHA-256"):
        validate_training_config(config)
