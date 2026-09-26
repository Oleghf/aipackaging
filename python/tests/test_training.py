"""Малый интеграционный тест поведенческого клонирования M3."""

from __future__ import annotations

from pathlib import Path

import numpy as np
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


def test_grid_checkpoint_requires_committed_state_for_resume(tmp_path: Path) -> None:
    """Старая клеточная точка читается, но не подтверждает безопасное продолжение."""

    from aipackaging_ml.training import load_checkpoint, save_checkpoint
    from aipackaging_ml.training_runtime import restore_committed_state

    model = torch.nn.Linear(1, 1)
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    path = tmp_path / "legacy-grid.pt"
    save_checkpoint(path, model, optimizer, scheduler, stage="ppo", step=1, config={"seed": 42})

    payload = load_checkpoint(path, model, torch.device("cpu"))
    assert payload["stage"] == "ppo"
    with pytest.raises(ValueError, match="не подтверждает завершённое обновление"):
        restore_committed_state(payload, "ppo")


def test_grid_checkpoint_rejects_config_before_model_change(tmp_path: Path) -> None:
    """Несовместимая конфигурация не изменяет целевую клеточную модель."""

    from aipackaging_ml.training import load_checkpoint, save_checkpoint

    source = torch.nn.Linear(1, 1)
    optimizer = torch.optim.AdamW(source.parameters(), lr=1e-3)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    path = tmp_path / "grid.pt"
    save_checkpoint(
        path,
        source,
        optimizer,
        scheduler,
        stage="bc",
        step=0,
        config={"seed": 42},
        training_state={"bcCommittedEpoch": 0, "history": [], "elapsedTrainingSeconds": 0.0},
    )
    target = torch.nn.Linear(1, 1)
    before = {name: value.clone() for name, value in target.state_dict().items()}

    with pytest.raises(ValueError, match="конфигурация контрольной точки"):
        load_checkpoint(path, target, torch.device("cpu"), expected_config={"seed": 43})
    assert all(torch.equal(value, before[name]) for name, value in target.state_dict().items())


def test_grid_bc_resume_matches_continuous_epochs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Откат и продолжение клеточной BC дают то же согласованное состояние."""

    from aipackaging_ml import training

    episode = _one_step_episode()
    config = {
        "seed": 42,
        "behavioralCloning": {
            "learningRate": 0.01,
            "weightDecay": 0.0,
            "maxEpochs": 2,
            "gradientAccumulation": 1,
            "earlyStoppingPatience": 10,
        },
    }
    continuous = tmp_path / "continuous"
    resumed = tmp_path / "resumed"

    configure_determinism(42)
    continuous_model = HierarchicalGridPolicyV1(hidden_size=16)
    training.train_behavioral_cloning(
        continuous_model, [episode], [episode], config, continuous, torch.device("cpu")
    )
    continuous_payload = training.load_checkpoint(
        continuous / "resume.pt", continuous_model, torch.device("cpu")
    )

    configure_determinism(42)
    resumed_model = HierarchicalGridPolicyV1(hidden_size=16)
    original_epoch = training._train_grid_bc_epoch
    calls = 0

    def interrupt_second_epoch(*args: object, **kwargs: object) -> tuple[int, float, bool]:
        """Выполняет первую эпоху и имитирует остановку до изменений второй."""

        nonlocal calls
        calls += 1
        if calls == 1:
            return original_epoch(*args, **kwargs)
        return 0, 0.0, True

    monkeypatch.setattr(training, "_train_grid_bc_epoch", interrupt_second_epoch)
    training.train_behavioral_cloning(
        resumed_model, [episode], [episode], config, resumed, torch.device("cpu")
    )
    monkeypatch.setattr(training, "_train_grid_bc_epoch", original_epoch)
    training.train_behavioral_cloning(
        resumed_model,
        [episode],
        [episode],
        config,
        resumed,
        torch.device("cpu"),
        resume=resumed / "resume.pt",
    )
    resumed_payload = training.load_checkpoint(
        resumed / "resume.pt", resumed_model, torch.device("cpu")
    )

    assert resumed_payload["trainingState"] == continuous_payload["trainingState"]
    assert resumed_payload["schedulerState"] == continuous_payload["schedulerState"]
    assert resumed_payload["pythonRandomState"] == continuous_payload["pythonRandomState"]
    assert np.array_equal(
        resumed_payload["numpyRandomState"][1], continuous_payload["numpyRandomState"][1]
    )
    for name, value in continuous_payload["modelState"].items():
        assert torch.equal(value, resumed_payload["modelState"][name])
