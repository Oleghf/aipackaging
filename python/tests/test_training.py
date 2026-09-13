"""Малый интеграционный тест поведенческого клонирования M3."""

from __future__ import annotations

from pathlib import Path

import pytest

torch = pytest.importorskip("torch")

from aipackaging_ml.environment import GridNestingEnv
from aipackaging_ml.model import HierarchicalGridPolicyV1
from aipackaging_ml.policy import evaluate_action
from aipackaging_ml.training import configure_determinism, train_behavioral_cloning
from aipackaging_ml.training_data import ExpertEpisode, replay_expert_steps


def _one_step_episode() -> ExpertEpisode:
    """Создаёт экспертный эпизод с выбором одной из двух допустимых позиций."""

    problem = {
        "format": "aipackaging.grid_problem",
        "version": 1,
        "problemId": "bc-overfit",
        "sheet": {"columns": 2, "rows": 1, "unit": "cell"},
        "parts": [{"id": "cell", "quantity": 1, "cells": [{"column": 0, "row": 0}], "allowedRotations": [0]}],
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    environment = GridNestingEnv.from_dict(problem)
    dynamic, _ = environment.reset_compact()
    action_index = int(next(index for index, legal in enumerate(dynamic["action_mask"]) if legal))
    _, reward, terminated, _, info = environment.step_compact(action_index)
    trajectory = {
        "trajectoryId": "bc-overfit-expert",
        "steps": [
            {
                "actionIndex": action_index,
                "reward": reward,
                "rankAfter": info["rankAfter"],
                "terminated": terminated,
            }
        ],
    }
    return ExpertEpisode("small", "train", problem, trajectory)


def test_behavioral_cloning_overfits_single_expert_decision(tmp_path: Path) -> None:
    """BC увеличивает вероятность единственного экспертного действия тестового примера."""

    configure_determinism(42)
    episode = _one_step_episode()
    sample = next(replay_expert_steps(episode))
    model = HierarchicalGridPolicyV1(hidden_size=16)
    device = torch.device("cpu")
    before = float(evaluate_action(model, sample.fixed, sample.dynamic, sample.action_index, device).log_probability.detach())
    config = {
        "seed": 42,
        "behavioralCloning": {
            "learningRate": 0.02,
            "weightDecay": 0.0,
            "maxEpochs": 40,
            "gradientAccumulation": 1,
            "earlyStoppingPatience": 40,
        },
    }
    train_behavioral_cloning(model, [episode], [episode], config, tmp_path, device)
    after = float(evaluate_action(model, sample.fixed, sample.dynamic, sample.action_index, device).log_probability.detach())
    assert after > before + 0.25
    assert after > -0.05
