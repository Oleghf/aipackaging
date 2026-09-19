"""Точное повторное проигрывание полигональных траекторий v1 через пространство действий C++."""

from __future__ import annotations

from typing import Any, Mapping

from ... import _aipackaging_solver as _native
from ...environment import PolygonNestingEnv
from ..serialization import canonical_json, require_keys


def verify_replay(problem: Mapping[str, Any], trajectory: Mapping[str, Any]) -> None:
    """Проверяет итоговое решение и воспроизводит все сохранённые действия траектории."""

    require_keys(
        trajectory,
        {"format", "version", "trajectoryId", "problemId", "solver", "actionIndices", "steps", "finalSolution"},
        "trajectory",
    )
    if trajectory["format"] != "aipackaging.polygon_trajectory" or trajectory["version"] != 1:
        raise ValueError("неподдерживаемый контракт `polygon_trajectory`")
    if trajectory["problemId"] != problem["problemId"]:
        raise ValueError(f"задача траектории не совпадает: {trajectory['trajectoryId']}")
    solution = trajectory["finalSolution"]
    if trajectory["solver"] != solution["solver"] or trajectory["trajectoryId"] != (
        f"{trajectory['problemId']}:{solution['solver']['name']}"
    ):
        raise ValueError(f"происхождение траектории не совпадает: {trajectory['trajectoryId']}")
    for metric in ("candidateGenerationTimeUs", "validationTimeUs", "searchTimeUs", "totalTimeUs"):
        if solution["metrics"][metric] != 0:
            raise ValueError(f"траектория содержит незамороженные временные метрики: {trajectory['trajectoryId']}")

    wire = canonical_json(problem)
    if _native.validate_polygon_solution(wire, canonical_json(solution)):
        raise ValueError(f"некорректное итоговое решение: {trajectory['trajectoryId']}")
    environment = PolygonNestingEnv.from_dict(problem, catalog_version=1)
    environment.reset()
    if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
        raise ValueError(f"проверочные индексы действий не совпадают: {trajectory['trajectoryId']}")
    if solution["placements"] != [step["action"] for step in trajectory["steps"]]:
        raise ValueError(f"проверочные размещения решения не совпадают: {trajectory['trajectoryId']}")
    for step in trajectory["steps"]:
        require_keys(step, {"actionIndex", "action", "reward", "terminated", "truncated", "info"}, "trajectory step")
        if environment.action(step["actionIndex"]) != step["action"]:
            raise ValueError(f"действие при повторном проигрывании не совпадает: {trajectory['trajectoryId']}")
        _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
        if (
            reward != step["reward"]
            or terminated != step["terminated"]
            or truncated != step["truncated"]
            or info != step["info"]
        ):
            raise ValueError(f"переход при повторном проигрывании не совпадает: {trajectory['trajectoryId']}")
