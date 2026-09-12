"""Точный replay полигональных trajectory v1 через динамический C++ action space."""

from __future__ import annotations

from typing import Any, Mapping

from ... import _aipackaging_solver as _native
from ...environment import PolygonNestingEnv
from ..serialization import canonical_json, require_keys


def verify_replay(problem: Mapping[str, Any], trajectory: Mapping[str, Any]) -> None:
    """Проверяет final solution и воспроизводит все сохранённые действия траектории."""

    require_keys(
        trajectory,
        {"format", "version", "trajectoryId", "problemId", "solver", "actionIndices", "steps", "finalSolution"},
        "trajectory",
    )
    if trajectory["format"] != "aipackaging.polygon_trajectory" or trajectory["version"] != 1:
        raise ValueError("unsupported polygon_trajectory")
    if trajectory["problemId"] != problem["problemId"]:
        raise ValueError(f"trajectory problem mismatch: {trajectory['trajectoryId']}")
    solution = trajectory["finalSolution"]
    if trajectory["solver"] != solution["solver"] or trajectory["trajectoryId"] != (
        f"{trajectory['problemId']}:{solution['solver']['name']}"
    ):
        raise ValueError(f"trajectory provenance mismatch: {trajectory['trajectoryId']}")
    for metric in ("candidateGenerationTimeUs", "validationTimeUs", "searchTimeUs", "totalTimeUs"):
        if solution["metrics"][metric] != 0:
            raise ValueError(f"trajectory contains non-frozen time metrics: {trajectory['trajectoryId']}")

    wire = canonical_json(problem)
    if _native.validate_polygon_solution(wire, canonical_json(solution)):
        raise ValueError(f"invalid final solution: {trajectory['trajectoryId']}")
    environment = PolygonNestingEnv.from_dict(problem)
    environment.reset()
    if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
        raise ValueError(f"action index audit mismatch: {trajectory['trajectoryId']}")
    if solution["placements"] != [step["action"] for step in trajectory["steps"]]:
        raise ValueError(f"solution placement audit mismatch: {trajectory['trajectoryId']}")
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
