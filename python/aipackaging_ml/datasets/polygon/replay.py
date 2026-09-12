"""Точный replay полигональных trajectory v1 через динамический C++ action space."""

from __future__ import annotations

from typing import Any, Mapping

from ... import _aipackaging_solver as _native
from ...environment import PolygonNestingEnv
from ..serialization import canonical_json, require_keys


def verify_replay(problem: Mapping[str, Any], trajectory: Mapping[str, Any]) -> None:
    """Проверяет final solution и воспроизводит все сохранённые действия траектории."""

    wire = canonical_json(problem)
    if _native.validate_polygon_solution(wire, canonical_json(trajectory["finalSolution"])):
        raise ValueError(f"invalid final solution: {trajectory['trajectoryId']}")
    environment = PolygonNestingEnv.from_dict(problem)
    environment.reset()
    if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
        raise ValueError(f"action index audit mismatch: {trajectory['trajectoryId']}")
    for step in trajectory["steps"]:
        require_keys(step, {"actionIndex", "action", "reward", "terminated", "truncated", "info"}, "trajectory step")
        if environment.action(step["actionIndex"]) != step["action"]:
            raise ValueError(f"action replay mismatch: {trajectory['trajectoryId']}")
        _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
        if (
            reward != step["reward"]
            or terminated != step["terminated"]
            or truncated != step["truncated"]
            or info != step["info"]
        ):
            raise ValueError(f"transition replay mismatch: {trajectory['trajectoryId']}")
