"""Контрактные тесты Python-обёртки полигональной среды M4."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from aipackaging_ml import PolygonNestingEnv

FIXTURE = Path(__file__).parent / "fixtures" / "polygon-smoke-problem.json"


def test_polygon_observation_and_dynamic_actions() -> None:
    """Наблюдения read-only, а action space пересчитывается после шага."""

    environment = PolygonNestingEnv.from_file(FIXTURE)
    observation, info = environment.reset(seed=42)
    assert observation["occupied"].shape == (128, 128)
    assert observation["clearance"].dtype == np.float32
    assert observation["remaining"].dtype == np.bool_
    assert observation["part_features"].shape == (2, 7)
    assert observation["objective"].shape == (7,)
    assert all(not value.flags.writeable for value in observation.values())
    initial_actions = environment.actions()
    assert info["actionCount"] == len(initial_actions) > 0
    _, reward, terminated, truncated, _ = environment.step(0)
    assert reward > 0 and not terminated and not truncated
    assert environment.actions() != initial_actions


def test_polygon_placement_observation_and_errors() -> None:
    """Условное наблюдение имеет четыре канала, ошибки не меняют каталог."""

    environment = PolygonNestingEnv.from_file(FIXTURE)
    environment.reset()
    placement = environment.placement_observation(0, 0)
    assert placement["raster"].shape == (4, 128, 128)
    assert placement["candidate_features"].shape[1] == 7
    before = environment.actions()
    with pytest.raises(IndexError):
        environment.step(len(before))
    assert environment.actions() == before
    with pytest.raises(ValueError):
        environment.placement_observation(0, 180)
