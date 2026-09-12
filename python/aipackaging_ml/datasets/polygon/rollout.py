"""Baseline rollout и выбор expert для полигонального датасета."""

from __future__ import annotations

import json
from typing import Any

from ... import _aipackaging_solver as _native
from ...environment import PolygonNestingEnv
from ..serialization import canonical_json

POLYGON_SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")


def best_trajectory(trajectories: list[dict[str, Any]]) -> dict[str, Any]:
    """Выбирает лучшую полигональную траекторию публичным C++-компаратором."""

    best = trajectories[0]
    for candidate in trajectories[1:]:
        if _native.is_better_polygon_solution(
            canonical_json(candidate["finalSolution"]), canonical_json(best["finalSolution"])
        ):
            best = candidate
    return best


def rollout_task(task: tuple[dict[str, Any], int]) -> tuple[str, list[dict[str, Any]], str]:
    """Строит и независимо перепроверяет пять baseline-траекторий polygon-задачи."""

    problem, seed = task
    wire = canonical_json(problem)
    trajectories = []
    for solver in POLYGON_SOLVERS:
        solution = json.loads(
            _native.solve_polygon_problem(
                wire,
                solver=solver,
                seed=seed,
                random_iterations=8,
                beam_width=4,
                max_expanded_states=100,
                timeout_ms=0,
            )
        )
        error = _native.validate_polygon_solution(wire, canonical_json(solution))
        if error:
            raise RuntimeError(f"{problem['problemId']}/{solver}: {error}")
        environment = PolygonNestingEnv.from_dict(problem)
        environment.reset(seed=seed)
        indices = []
        steps = []
        for placement in solution["placements"]:
            index = environment.find_action(placement)
            action = environment.action(index)
            _, reward, terminated, truncated, info = environment.step(index)
            indices.append(index)
            steps.append(
                {
                    "actionIndex": index,
                    "action": action,
                    "reward": reward,
                    "terminated": terminated,
                    "truncated": truncated,
                    "info": info,
                }
            )
        trajectories.append(
            {
                "format": "aipackaging.polygon_trajectory",
                "version": 1,
                "trajectoryId": f"{problem['problemId']}:{solver}",
                "problemId": problem["problemId"],
                "solver": solution["solver"],
                "actionIndices": indices,
                "steps": steps,
                "finalSolution": solution,
            }
        )
    expert = best_trajectory(trajectories)["trajectoryId"]
    return problem["problemId"], trajectories, expert
