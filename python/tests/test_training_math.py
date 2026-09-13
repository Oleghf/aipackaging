"""Тесты формул обучения M3 на малых синтетических переходах."""

from __future__ import annotations

import pytest

torch = pytest.importorskip("torch")

from aipackaging_ml.rl import PpoTransition, compute_gae


def _transition(reward: float, value: float, next_value: float, terminated: bool) -> PpoTransition:
    """Создаёт переход только с полями, участвующими в GAE."""

    return PpoTransition({}, {}, 0, 0.0, value, reward, next_value, terminated)


def test_gae_resets_at_terminal_and_returns_critic_targets() -> None:
    """Завершающий переход не протягивает преимущество следующего эпизода назад."""

    transitions = [
        _transition(1.0, 0.5, 0.25, False),
        _transition(2.0, 0.25, 0.0, True),
        _transition(4.0, 1.0, 0.0, True),
    ]
    advantages, returns = compute_gae(transitions, gamma=1.0, gae_lambda=1.0)
    assert advantages.tolist() == pytest.approx([2.5, 1.75, 3.0])
    assert returns.tolist() == pytest.approx([3.0, 2.0, 4.0])


def test_gae_keeps_bootstrap_but_stops_at_parallel_trace_boundary() -> None:
    """Конец канала траектории использует следующее значение без утечки из соседнего канала."""

    first_lane = PpoTransition({}, {}, 0, 0.0, 0.5, 1.0, 0.25, False, True)
    second_lane = PpoTransition({}, {}, 0, 0.0, 10.0, 20.0, 0.0, True)
    advantages, returns = compute_gae([first_lane, second_lane], gamma=1.0, gae_lambda=1.0)
    assert advantages.tolist() == pytest.approx([0.75, 10.0])
    assert returns.tolist() == pytest.approx([1.25, 20.0])
