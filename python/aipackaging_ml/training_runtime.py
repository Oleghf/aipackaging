"""Общие правила бюджета, истории и согласованных границ обучения."""

from __future__ import annotations

import math
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Sequence

from .datasets.serialization import canonical_json


@dataclass(frozen=True)
class TrainingBudget:
    """Учитывает единый временной бюджет во всех продолжениях обучения."""

    limit_seconds: float
    elapsed_before_seconds: float
    invocation_started: float

    @property
    def deadline(self) -> float:
        """Возвращает момент исчерпания оставшейся части общего бюджета."""

        return self.invocation_started + max(0.0, self.limit_seconds - self.elapsed_before_seconds)

    def exhausted(self) -> bool:
        """Сообщает, исчерпан ли бюджет к текущему моменту."""

        return time.monotonic() >= self.deadline

    def elapsed_seconds(self) -> float:
        """Возвращает накопленное время, ограниченное полным бюджетом."""

        current = self.elapsed_before_seconds + max(0.0, time.monotonic() - self.invocation_started)
        return min(self.limit_seconds, current)

    def checkpoint_state(self, values: Mapping[str, Any]) -> dict[str, Any]:
        """Добавляет накопленное время к состоянию контрольной точки."""

        result = dict(values)
        result["elapsedTrainingSeconds"] = self.elapsed_seconds()
        return result


def resume_elapsed_seconds(payload: Mapping[str, Any], limit_seconds: float) -> float:
    """Проверяет накопленное время перед продолжением контрольной точки."""

    training_state = payload.get("trainingState")
    if not isinstance(training_state, Mapping) or "elapsedTrainingSeconds" not in training_state:
        raise ValueError(
            "контрольная точка не содержит накопленное время; её можно оценить или завершить, но нельзя продолжить"
        )
    elapsed = training_state["elapsedTrainingSeconds"]
    if isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or not math.isfinite(float(elapsed)):
        raise ValueError("контрольная точка содержит некорректное накопленное время")
    elapsed_value = float(elapsed)
    if elapsed_value < 0 or elapsed_value > limit_seconds:
        raise ValueError("накопленное время контрольной точки выходит за общий бюджет")
    if elapsed_value >= limit_seconds:
        raise ValueError("общий временной бюджет обучения уже исчерпан")
    return elapsed_value


def committed_state(
    stage: str,
    step: int,
    history: Sequence[Mapping[str, Any]],
    *,
    budget: TrainingBudget | None,
    values: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Формирует состояние завершённой эпохи BC либо обновления PPO."""

    if stage not in {"bc", "ppo"}:
        raise ValueError("неизвестный этап обучения")
    result = dict(values or {})
    result["bcCommittedEpoch" if stage == "bc" else "ppoCommittedUpdate"] = step
    result["history"] = [dict(item) for item in history]
    return budget.checkpoint_state(result) if budget is not None else result


def restore_committed_state(payload: Mapping[str, Any], stage: str) -> tuple[int, list[dict[str, Any]], dict[str, Any]]:
    """Возвращает только строго подтверждённую границу и полную историю этапа."""

    if payload.get("stage") != stage:
        raise ValueError(f"для продолжения {stage.upper()} требуется контрольная точка этапа {stage.upper()}")
    step = payload.get("step")
    state = payload.get("trainingState")
    key = "bcCommittedEpoch" if stage == "bc" else "ppoCommittedUpdate"
    if (
        isinstance(step, bool)
        or not isinstance(step, int)
        or step < 0
        or not isinstance(state, Mapping)
        or state.get(key) != step
        or not isinstance(state.get("history"), list)
        or any(not isinstance(item, Mapping) for item in state["history"])
    ):
        boundary = "завершённую эпоху" if stage == "bc" else "завершённое обновление"
        raise ValueError(
            f"контрольная точка {stage.upper()} не подтверждает {boundary}; продолжение небезопасно"
        )
    history = [dict(item) for item in state["history"]]
    return step, history, dict(state)


def write_metrics_history(path: str | Path, history: Sequence[Mapping[str, Any]]) -> None:
    """Атомарно записывает полную подтверждённую историю в канонический JSONL."""

    destination = Path(path)
    temporary = destination.with_suffix(destination.suffix + ".tmp")
    temporary.write_text(
        "".join(canonical_json(dict(item)) + "\n" for item in history),
        encoding="utf-8",
        newline="\n",
    )
    temporary.replace(destination)
