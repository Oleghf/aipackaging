"""Общее атомарное хранение доверенных контрольных точек обучения."""

from __future__ import annotations

import random
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

import numpy as np
import torch

from .datasets.serialization import sha256_file


@dataclass(frozen=True)
class CheckpointContract:
    """Описывает неизменяемую оболочку контрольной точки конкретной среды."""

    format: str
    unsupported_message: str
    strict_fields: bool
    write_sha256_sidecar: bool
    require_training_state: bool = False


def build_checkpoint_payload(
    contract: CheckpointContract,
    model: torch.nn.Module,
    optimizer: torch.optim.Optimizer,
    scheduler: torch.optim.lr_scheduler.LRScheduler,
    *,
    stage: str,
    step: int,
    config: Mapping[str, Any],
    training_state: Mapping[str, Any] | None,
) -> dict[str, Any]:
    """Собирает оболочку и состояния вычислений в прежнем порядке полей."""

    payload: dict[str, Any] = {
        "format": contract.format,
        "version": 1,
        "stage": stage,
        "step": step,
        "config": dict(config),
        "modelState": model.state_dict(),
        "optimizerState": optimizer.state_dict(),
        "schedulerState": scheduler.state_dict(),
        "pythonRandomState": random.getstate(),
        "numpyRandomState": np.random.get_state(),
        "torchRandomState": torch.get_rng_state(),
    }
    if training_state is not None:
        payload["trainingState"] = dict(training_state)
    if torch.cuda.is_available():
        payload["cudaRandomState"] = torch.cuda.get_rng_state_all()
    return payload


def save_training_checkpoint(
    path: str | Path,
    contract: CheckpointContract,
    model: torch.nn.Module,
    optimizer: torch.optim.Optimizer,
    scheduler: torch.optim.lr_scheduler.LRScheduler,
    *,
    stage: str,
    step: int,
    config: Mapping[str, Any],
    training_state: Mapping[str, Any] | None = None,
) -> str:
    """Атомарно сохраняет точку и при необходимости её отдельный SHA-256."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    torch.save(
        build_checkpoint_payload(
            contract,
            model,
            optimizer,
            scheduler,
            stage=stage,
            step=step,
            config=config,
            training_state=training_state,
        ),
        temporary,
    )
    temporary.replace(destination)
    digest = sha256_file(destination)
    if contract.write_sha256_sidecar:
        digest_path = destination.with_suffix(destination.suffix + ".sha256")
        digest_temporary = digest_path.with_suffix(digest_path.suffix + ".tmp")
        digest_temporary.write_text(digest + "\n", encoding="ascii", newline="\n")
        digest_temporary.replace(digest_path)
    return digest


def _expected_digest(path: Path, expected_sha256: str | None) -> str | None:
    """Проверяет запись ожидаемого SHA-256 до загрузки содержимого."""

    if expected_sha256 is None:
        return None
    normalized = expected_sha256.lower()
    if len(normalized) != 64 or any(character not in "0123456789abcdef" for character in normalized):
        raise ValueError("некорректная запись SHA-256 контрольной точки")
    if sha256_file(path) != normalized:
        raise ValueError("контрольная сумма контрольной точки не совпадает")
    return normalized


def load_training_checkpoint(
    path: str | Path,
    contract: CheckpointContract,
    model: torch.nn.Module,
    device: torch.device,
    *,
    optimizer: torch.optim.Optimizer | None = None,
    scheduler: torch.optim.lr_scheduler.LRScheduler | None = None,
    restore_rng: bool = False,
    expected_sha256: str | None = None,
    expected_config: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Полностью проверяет оболочку до публикации её состояния в вызывающие объекты."""

    source = Path(path)
    if contract.write_sha256_sidecar and expected_sha256 is None:
        digest_path = source.with_suffix(source.suffix + ".sha256")
        if not digest_path.is_file():
            raise ValueError("рядом с контрольной точкой отсутствует файл SHA-256")
        expected_sha256 = digest_path.read_text(encoding="ascii").strip()
    _expected_digest(source, expected_sha256)

    # Контрольная точка содержит состояние оптимизатора и RNG, поэтому API
    # предназначен только для доверенных локальных артефактов проекта.
    payload = torch.load(source, map_location=device, weights_only=False)
    if not isinstance(payload, Mapping) or payload.get("format") != contract.format or payload.get("version") != 1:
        raise ValueError(contract.unsupported_message)
    required = {
        "format",
        "version",
        "stage",
        "step",
        "config",
        "modelState",
        "optimizerState",
        "schedulerState",
        "pythonRandomState",
        "numpyRandomState",
        "torchRandomState",
    }
    if contract.require_training_state:
        required.add("trainingState")
    allowed = required | {"trainingState", "cudaRandomState"}
    if contract.strict_fields and (not required <= set(payload) or not set(payload) <= allowed):
        raise ValueError("контрольная точка содержит неизвестные или пропущенные поля")
    if payload.get("stage") not in {"bc", "ppo"} or isinstance(payload.get("step"), bool) or not isinstance(payload.get("step"), int) or payload["step"] < 0:
        raise ValueError("контрольная точка содержит некорректный этап обучения")
    if expected_config is not None and payload.get("config") != dict(expected_config):
        raise ValueError("конфигурация контрольной точки не совпадает с запрошенным продолжением")

    model.load_state_dict(payload["modelState"])
    if optimizer is not None:
        optimizer.load_state_dict(payload["optimizerState"])
    if scheduler is not None:
        scheduler.load_state_dict(payload["schedulerState"])
    if restore_rng:
        random.setstate(payload["pythonRandomState"])
        np.random.set_state(payload["numpyRandomState"])
        torch.set_rng_state(payload["torchRandomState"].cpu())
        if torch.cuda.is_available() and "cudaRandomState" in payload:
            torch.cuda.set_rng_state_all([state.cpu() for state in payload["cudaRandomState"]])
    return dict(payload)
