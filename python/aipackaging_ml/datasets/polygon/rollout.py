"""Запуск базовых алгоритмов и выбор экспертного результата для полигонального набора."""

from __future__ import annotations

import json
from typing import Any

from ... import _aipackaging_solver as _native
from ...environment import PolygonNestingEnv
from ..serialization import canonical_json

POLYGON_SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")
POLYGON_BUDGETS = {"timeoutMs": 0, "randomIterations": 64, "beamWidth": 8, "maxExpandedStates": 5_000}
POLYGON_SMOKE_BUDGETS = {"timeoutMs": 0, "randomIterations": 2, "beamWidth": 2, "maxExpandedStates": 100}


def best_trajectory(trajectories: list[dict[str, Any]]) -> dict[str, Any]:
    """Выбирает лучшую полигональную траекторию публичным C++-компаратором."""

    best = trajectories[0]
    for candidate in trajectories[1:]:
        if _native.is_better_polygon_solution(
            canonical_json(candidate["finalSolution"]), canonical_json(best["finalSolution"])
        ):
            best = candidate
    return best


def _freeze_time_metrics(solution: dict[str, Any]) -> None:
    """Обнуляет машинно-зависимые времена перед сохранением замороженной траектории."""

    for name in ("candidateGenerationTimeUs", "validationTimeUs", "searchTimeUs", "totalTimeUs"):
        solution["metrics"][name] = 0


def rollout_problem(
    problem: dict[str, Any], seed: int, budgets: dict[str, int], *, require_solved: bool
) -> tuple[list[dict[str, Any]], str]:
    """Строит пять корректных траекторий базовых алгоритмов с заданными бюджетами."""

    wire = canonical_json(problem)
    trajectories = []
    for solver in POLYGON_SOLVERS:
        solution = json.loads(
            _native.solve_polygon_problem(
                wire,
                solver=solver,
                seed=seed,
                random_iterations=budgets["randomIterations"],
                beam_width=budgets["beamWidth"],
                max_expanded_states=budgets["maxExpandedStates"],
                timeout_ms=budgets["timeoutMs"],
            )
        )
        _freeze_time_metrics(solution)
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
            steps.append({
                "actionIndex": index,
                "action": action,
                "reward": reward,
                "terminated": terminated,
                "truncated": truncated,
                "info": info,
            })
        trajectories.append({
            "format": "aipackaging.polygon_trajectory",
            "version": 1,
            "trajectoryId": f"{problem['problemId']}:{solver}",
            "problemId": problem["problemId"],
            "solver": solution["solver"],
            "actionIndices": indices,
            "steps": steps,
            "finalSolution": solution,
        })

    solved = [item for item in trajectories if item["finalSolution"]["status"] == "solved"]
    if require_solved and not solved:
        raise RuntimeError("ни один базовый алгоритм не нашёл полного решения")
    eligible = solved if solved else trajectories
    return trajectories, best_trajectory(eligible)["trajectoryId"]


def rollout_task(task: tuple[dict[str, Any], int]) -> tuple[str, list[dict[str, Any]], str]:
    """Строит и независимо проверяет пять траекторий базовых алгоритмов полигональной задачи."""

    problem, seed = task
    budgets = {"timeoutMs": 0, "randomIterations": 8, "beamWidth": 4, "maxExpandedStates": 100}
    trajectories, expert = rollout_problem(problem, seed, budgets, require_solved=False)
    return problem["problemId"], trajectories, expert
