"""Запуск базовых алгоритмов и выбор экспертного результата для клеточного набора."""

from __future__ import annotations

import json
from typing import Any

from ... import _aipackaging_solver as _native
from ...environment import GridNestingEnv
from ..serialization import canonical_json

SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")


def best_trajectory(trajectories: list[dict[str, Any]]) -> dict[str, Any]:
    """Выбирает лучшую непустую последовательность публичным C++-компаратором."""

    best = trajectories[0]
    for candidate in trajectories[1:]:
        if _native.is_better_solution(
            canonical_json(candidate["finalSolution"]), canonical_json(best["finalSolution"])
        ):
            best = candidate
    return best


def rollout_task(task: tuple[dict[str, Any], int]) -> tuple[str, list[dict[str, Any]], str]:
    """Строит и независимо проверяет пять траекторий базовых алгоритмов клеточной задачи."""

    problem, seed = task
    problem_json = canonical_json(problem)
    trajectories: list[dict[str, Any]] = []
    for solver in SOLVERS:
        solution = json.loads(
            _native.solve_problem(
                problem_json,
                solver=solver,
                seed=seed,
                random_iterations=64,
                beam_width=32,
                max_expanded_states=50_000,
                timeout_ms=0,
            )
        )
        validation_error = _native.validate_solution(problem_json, canonical_json(solution))
        if validation_error:
            raise RuntimeError(f"{problem['problemId']}/{solver}: {validation_error}")

        environment = GridNestingEnv.from_dict(problem)
        environment.reset(seed=seed)
        steps = []
        for placement in solution["placements"]:
            action_index = environment.find_action(placement)
            action = environment.action(action_index)
            _, reward, terminated, truncated, info = environment.step(action_index)
            steps.append(
                {
                    "actionIndex": action_index,
                    "action": action,
                    "reward": reward,
                    "rankBefore": info["rankBefore"],
                    "rankAfter": info["rankAfter"],
                    "deltas": info["deltas"],
                    "terminated": terminated,
                    "truncated": truncated,
                }
            )
        trajectory_id = f"{problem['problemId']}:{solver}"
        trajectories.append(
            {
                "format": "aipackaging.grid_trajectory",
                "version": 1,
                "trajectoryId": trajectory_id,
                "problemId": problem["problemId"],
                "solver": solution["solver"],
                "actionIndices": [step["actionIndex"] for step in steps],
                "steps": steps,
                "finalSolution": solution,
            }
        )
    expert = best_trajectory(trajectories)["trajectoryId"]
    return problem["problemId"], trajectories, expert
