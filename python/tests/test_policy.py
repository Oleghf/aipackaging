"""Интеграционные тесты иерархической политики и нативной маски действий."""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

torch = pytest.importorskip("torch")

from aipackaging_ml.environment import GridNestingEnv
from aipackaging_ml.model import HierarchicalGridPolicyV1
from aipackaging_ml.policy import PolicyRunner, evaluate_action, select_action

FIXTURE = Path(__file__).parent / "fixtures" / "smoke-problem.json"


def test_greedy_and_seeded_selection_never_bypass_action_mask() -> None:
    """Оба режима возвращают только разрешённый стабильный индекс действия."""

    environment = GridNestingEnv.from_file(FIXTURE)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    model = HierarchicalGridPolicyV1(hidden_size=32)
    greedy = select_action(model, fixed, dynamic, torch.device("cpu"))
    assert dynamic["action_mask"][greedy.action_index]
    assert evaluate_action(model, fixed, dynamic, greedy.action_index, torch.device("cpu")).action_index == greedy.action_index

    first_generator = torch.Generator().manual_seed(42)
    second_generator = torch.Generator().manual_seed(42)
    first = select_action(model, fixed, dynamic, torch.device("cpu"), first_generator)
    second = select_action(model, fixed, dynamic, torch.device("cpu"), second_generator)
    assert first.action_index == second.action_index
    assert np.asarray(dynamic["action_mask"])[first.action_index]


def test_hybrid_runner_returns_valid_solution_v2_with_auditable_provenance() -> None:
    """Гибрид безопасно выбирает между политикой и базовым алгоритмом и отмечает результат."""

    import json

    from aipackaging_ml import _aipackaging_solver as native
    from aipackaging_ml.environment import canonical_json

    problem = json.loads(FIXTURE.read_text(encoding="utf-8"))
    runner = PolicyRunner(
        HierarchicalGridPolicyV1(hidden_size=32),
        model_id="fixture-policy",
        model_sha256="a" * 64,
    )
    solution = runner.solve(problem, mode="hybrid", rollouts=2, seed=42)
    assert solution["version"] == 2
    assert solution["status"] == "solved"
    assert solution["solver"]["family"] == "hybrid"
    assert solution["solver"]["policy"]["selectionMode"] == "hybrid-best-of"
    assert native.validate_solution(canonical_json(problem), canonical_json(solution)) == ""


def test_neural_dead_end_is_not_reported_as_budget_exhaustion() -> None:
    """Естественный тупик политики получает честный статус `no_solution_found`."""

    problem = {
        "format": "aipackaging.grid_problem",
        "version": 1,
        "problemId": "policy-dead-end",
        "sheet": {"columns": 2, "rows": 1, "unit": "cell"},
        "parts": [
            {
                "id": "domino",
                "quantity": 2,
                "cells": [{"column": 0, "row": 0}, {"column": 1, "row": 0}],
                "allowedRotations": [0],
            }
        ],
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    runner = PolicyRunner(HierarchicalGridPolicyV1(hidden_size=32), model_id="fixture", model_sha256="b" * 64)
    solution = runner.solve(problem, mode="greedy", seed=42)
    assert solution["status"] == "no_solution_found"
    assert solution["objective"]["placedParts"] == 1
