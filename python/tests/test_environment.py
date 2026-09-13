"""Контрактные тесты оболочки Python и наблюдения NumPy v1."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from aipackaging_ml import GridNestingEnv

FIXTURE = Path(__file__).parent / "fixtures" / "smoke-problem.json"


def _environment() -> GridNestingEnv:
    """Создаёт новый эпизод из фиксированной тестовой задачи."""

    return GridNestingEnv.from_file(FIXTURE)


def test_observation_shapes_dtypes_and_read_only() -> None:
    """Все публичные массивы имеют закреплённые форму и тип и запрещают запись."""

    environment = _environment()
    observation, info = environment.reset(seed=7)
    assert observation["occupancy"].shape == (2, 3)
    assert observation["occupancy"].dtype == np.uint8
    assert observation["part_masks"].shape == (2, 4, 2, 2)
    assert observation["orientation_mask"].dtype == np.bool_
    assert observation["remaining"].shape == (2,)
    assert observation["part_features"].shape == (2, 7)
    assert observation["part_features"].dtype == np.float32
    assert observation["candidate_instance"].dtype == np.int32
    assert observation["candidate_rotation"].dtype == np.uint8
    assert observation["candidate_features"].shape == (environment.action_count, 7)
    assert observation["action_mask"].dtype == np.bool_
    assert observation["objective"].shape == (7,)
    assert all(not value.flags.writeable for value in observation.values())
    assert info["seed"] == 7


def test_gym_like_step_and_exceptions_preserve_state() -> None:
    """Ошибочные индексы не меняют ранг, а корректные шаги завершают эпизод."""

    environment = _environment()
    observation, _ = environment.reset()
    first = int(np.flatnonzero(observation["action_mask"])[0])
    _, reward, terminated, truncated, info = environment.step(first)
    assert reward > 0
    assert not terminated
    assert not truncated
    assert info["rankAfter"] > info["rankBefore"]
    rank = environment.rank
    with pytest.raises(IndexError):
        environment.step(environment.action_count)
    assert environment.rank == rank
    with pytest.raises(ValueError):
        environment.step(first)
    assert environment.rank == rank

    action = {"partId": "single", "instanceIndex": 0, "column": 2, "row": 0, "rotationDegrees": 0}
    _, _, terminated, truncated, info = environment.step(environment.find_action(action))
    assert terminated and not truncated and info["complete"]
    with pytest.raises(RuntimeError):
        environment.step(0)


def test_from_dict_is_strict_and_limits_are_separate_from_geometry() -> None:
    """Строгий JSON и ограничения среды возвращают ValueError с диагностикой."""

    problem = json.loads(FIXTURE.read_text(encoding="utf-8"))
    problem["unknown"] = True
    with pytest.raises(ValueError, match="unknown"):
        GridNestingEnv.from_dict(problem)
    problem.pop("unknown")
    with pytest.raises(ValueError, match="action catalog"):
        GridNestingEnv.from_dict(problem, max_actions=1)


def test_compact_observation_and_neural_snapshot_v2() -> None:
    """Компактный API сохраняет динамические поля и формирует корректное решение v2."""

    environment = _environment()
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact(seed=42)
    assert set(fixed) == {
        "rows",
        "columns",
        "max_part_rows",
        "max_part_columns",
        "part_masks",
        "orientation_mask",
        "part_features",
        "candidate_instance",
        "candidate_rotation",
        "candidate_features",
    }
    assert set(dynamic) == {"occupancy", "remaining", "action_mask", "objective"}
    action = int(np.flatnonzero(dynamic["action_mask"])[0])
    dynamic, _, _, _, _ = environment.step_compact(action)
    assert dynamic["remaining"].sum() == 1

    provenance = {
        "family": "neural",
        "name": "grid-policy-v1",
        "projectVersion": "test",
        "revision": "test",
        "seed": 42,
        "randomIterations": 64,
        "beamWidth": 32,
        "maxExpandedStates": 50000,
        "timeoutMs": 0,
        "modelId": "fixture",
        "modelSha256": "a" * 64,
        "rollouts": 1,
        "selectionMode": "greedy",
    }
    solution = environment.snapshot_solution(provenance)
    assert solution["version"] == 2
    assert solution["solver"]["family"] == "neural"
