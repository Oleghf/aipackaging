"""Контрактные тесты Python-обёртки полигональной среды M4."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from aipackaging_ml import PolygonNestingEnv

FIXTURE = Path(__file__).parent / "fixtures" / "polygon-smoke-problem.json"


def test_polygon_observation_and_dynamic_actions() -> None:
    """Наблюдения доступны только для чтения, а пространство действий обновляется после шага."""

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


def test_catalog_versions_preserve_old_actions_and_add_line_contact() -> None:
    """Старый каталог сохраняет индексы, а исправленный включает внутренний контакт."""

    problem = json.loads(FIXTURE.read_text(encoding="utf-8"))
    problem["sheet"] = {"width": 10.0, "height": 30.0, "unit": "mm"}
    problem["manufacturing"]["sheetMargin"] = 0.0
    problem["manufacturing"]["partSpacing"] = 0.0

    def rectangle(part_id: str, quantity: int, height: float) -> dict:
        """Создаёт прямоугольную деталь шириной листа для проверки контакта."""

        return {"id": part_id, "quantity": quantity, "outer": {
            "start": {"x": 0.0, "y": 0.0}, "segments": [
                {"type": "line", "end": {"x": 10.0, "y": 0.0}},
                {"type": "line", "end": {"x": 10.0, "y": height}},
                {"type": "line", "end": {"x": 0.0, "y": height}},
                {"type": "line", "end": {"x": 0.0, "y": 0.0}},
            ]}, "holes": [], "allowedRotations": [0]}

    problem["parts"] = [rectangle("small", 2, 5.0), rectangle("big", 1, 10.0)]
    old = PolygonNestingEnv.from_dict(problem, catalog_version=1)
    corrected = PolygonNestingEnv.from_dict(problem, catalog_version=2)
    default = PolygonNestingEnv.from_dict(problem)
    for environment in (old, corrected, default):
        environment.reset()
        for index, y in enumerate((0, 25000)):
            action = {"partId": "small", "instanceIndex": index, "xMicrometers": 0,
                      "yMicrometers": y, "rotationDegrees": 0}
            environment.step(environment.find_action(action))
    assert old.actions() == []
    assert {"partId": "big", "instanceIndex": 0, "xMicrometers": 0,
            "yMicrometers": 5000, "rotationDegrees": 0} in corrected.actions()
    assert default.actions() == corrected.actions()
    with pytest.raises(ValueError, match="версия"):
        PolygonNestingEnv.from_dict(problem, catalog_version=3)
