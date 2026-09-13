"""Чистые структуры и математические операции PPO без нативных зависимостей."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Mapping, Sequence

import numpy as np


@dataclass(frozen=True)
class PpoTransition:
    """Хранит переход текущей политики и данные для повторной оценки PPO."""

    fixed: Mapping[str, Any]
    dynamic: Mapping[str, Any]
    action_index: int
    old_log_probability: float
    old_value: float
    reward: float
    next_value: float
    terminated: bool
    trace_end: bool = False


def compute_gae(
    transitions: Sequence[PpoTransition], gamma: float, gae_lambda: float
) -> tuple[np.ndarray, np.ndarray]:
    """Вычисляет GAE независимо для эпизодов и параллельных траекторий."""

    advantages = np.zeros(len(transitions), dtype=np.float32)
    running = 0.0
    for index in range(len(transitions) - 1, -1, -1):
        transition = transitions[index]
        bootstrap = 0.0 if transition.terminated else 1.0
        delta = transition.reward + gamma * transition.next_value * bootstrap - transition.old_value
        # На границе буфера траектории начальная оценка критика остаётся в приращении,
        # но GAE следующего независимого канала не должен попадать в текущую трассу.
        continuation = 0.0 if transition.terminated or transition.trace_end else 1.0
        running = delta + gamma * gae_lambda * continuation * running
        advantages[index] = running
    returns = advantages + np.asarray([item.old_value for item in transitions], dtype=np.float32)
    return advantages, returns
