"""Проверки наблюдений, модели и контрактов полигонального обучения M6.2."""

from __future__ import annotations

import json
import time
from pathlib import Path

import numpy as np
import pytest
from jsonschema import Draft202012Validator

from aipackaging_ml.environment import PolygonNestingEnv
from aipackaging_ml import _aipackaging_solver as _native
from aipackaging_ml.datasets.serialization import canonical_json
from aipackaging_ml.polygon_contracts import load_polygon_training_config, validate_polygon_training_config

FIXTURES = Path(__file__).parent / "fixtures"
ROOT = Path(__file__).parents[2]


def _problem() -> dict:
    """Загружает малую полигональную задачу для быстрых проверок."""

    return json.loads((FIXTURES / "polygon-smoke-problem.json").read_text(encoding="utf-8"))


def test_compact_observations_are_read_only() -> None:
    """Компактные наблюдения имеют закреплённые формы, типы и защиту от записи."""

    environment = PolygonNestingEnv.from_dict(_problem(), reward_version=2)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    assert fixed["part_masks"].shape == (2, 4, 32, 32)
    assert fixed["part_masks"].dtype == np.uint8
    assert fixed["orientation_mask"].dtype == np.bool_
    assert dynamic["pair_mask"].shape == (2, 4)
    assert dynamic["occupied"].shape == (128, 128)
    assert not fixed["part_masks"].flags.writeable
    assert not dynamic["pair_mask"].flags.writeable


def test_reward_v2_reports_exact_potential_delta() -> None:
    """Вознаграждение v2 совпадает с разностью записанных потенциалов."""

    environment = PolygonNestingEnv.from_dict(_problem(), reward_version=2)
    environment.reset_compact()
    _, reward, _, truncated, info = environment.step_compact(0)
    assert not truncated
    assert info["rewardVersion"] == 2
    assert reward == pytest.approx(info["potentialAfter"] - info["potentialBefore"], abs=1e-15)
    assert len(info["componentDeltas"]) == 5


def test_polygon_model_selects_only_legal_action() -> None:
    """Иерархическая политика не может выбрать запрещённую пару или позицию."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_policy import select_polygon_action

    environment = PolygonNestingEnv.from_dict(_problem(), reward_version=2)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    model = HierarchicalPolygonPolicyV1(32)
    decision = select_polygon_action(model, environment, fixed, dynamic, torch.device("cpu"))
    assert 0 <= decision.action_index < environment.action_count
    assert dynamic["pair_mask"][decision.instance_position, decision.rotation_slot]
    decision.log_probability.backward()
    assert any(parameter.grad is not None for parameter in model.encoder.instance_head.parameters())
    assert any(parameter.grad is not None for parameter in model.encoder.rotation_head.parameters())
    assert any(parameter.grad is not None for parameter in model.placement_head.parameters())


def test_polygon_training_config_is_strict() -> None:
    """Каноническая конфигурация принимается, а неизвестное поле отклоняется."""

    config = load_polygon_training_config(ROOT / "configs" / "m6" / "polygon-policy-v1.json")
    schema = json.loads((ROOT / "schemas" / "polygon-training-config-v1.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema).validate(config)
    assert config["rewardVersion"] == 2
    config["unknown"] = True
    with pytest.raises(ValueError, match="ожидались поля"):
        validate_polygon_training_config(config)


def test_unknown_reward_version_is_rejected() -> None:
    """Python-фабрика передаёт отказ неизвестной версии вознаграждения."""

    with pytest.raises(ValueError, match="версии"):
        PolygonNestingEnv.from_dict(_problem(), reward_version=3)


def test_polygon_checkpoint_restores_model_and_rng(tmp_path: Path) -> None:
    """Контрольная точка восстанавливает веса, оптимизатор и генераторы случайных чисел."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_training import load_polygon_checkpoint, save_polygon_checkpoint
    from aipackaging_ml.training import configure_determinism

    configure_determinism(42)
    model = HierarchicalPolygonPolicyV1(32)
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    path = tmp_path / "checkpoint.pt"
    digest = save_polygon_checkpoint(path, model, optimizer, scheduler, stage="ppo", step=3,
                                     config={"seed": 42}, training_state={"bestScore": [1.0, 0.0]})
    expected = {name: value.detach().clone() for name, value in model.state_dict().items()}
    with torch.no_grad():
        next(model.parameters()).add_(1.0)
    payload = load_polygon_checkpoint(path, model, torch.device("cpu"), optimizer=optimizer, scheduler=scheduler,
                                      restore_rng=True, expected_sha256=digest)
    assert payload["step"] == 3
    assert payload["trainingState"]["bestScore"] == [1.0, 0.0]
    assert all(torch.equal(model.state_dict()[name], value) for name, value in expected.items())
    path.with_suffix(".pt.sha256").write_text("0" * 64 + "\n", encoding="ascii")
    with pytest.raises(ValueError, match="контрольная сумма"):
        load_polygon_checkpoint(path, model, torch.device("cpu"))


def _expert_episode():
    """Строит полный экспертный эпизод из проверенного базового решения."""

    from aipackaging_ml.polygon_training_data import PolygonExpertEpisode

    problem = _problem()
    solution = json.loads(
        _native.solve_polygon_problem(canonical_json(problem), solver="area-left-bottom", timeout_ms=0)
    )
    assert solution["status"] == "solved"
    trajectory = {
        "trajectoryId": "test-expert",
        "steps": [{"action": placement} for placement in solution["placements"]],
    }
    return PolygonExpertEpisode("small", "train", problem, trajectory)


def test_polygon_observation_cache_round_trip(tmp_path: Path) -> None:
    """Кэш сохраняет подготовленные цели BC и защищает прочитанные массивы от записи."""

    from aipackaging_ml.polygon_training_cache import (
        read_polygon_observation_cache,
        write_polygon_observation_cache,
    )

    path = tmp_path / "observations.zip"
    written = write_polygon_observation_cache(path, {"train": [_expert_episode()]}, "a" * 64)
    loaded = read_polygon_observation_cache(path, "a" * 64)
    assert len(loaded["train"]) == len(written["train"]) == 2
    assert loaded["train"][0].instance == written["train"][0].instance
    assert loaded["train"][0].value_target == pytest.approx(written["train"][0].value_target)
    assert not loaded["train"][0].dynamic["pair_mask"].flags.writeable


def test_polygon_ppo_collection_uses_reproducible_worker_processes() -> None:
    """Повторный параллельный сбор с тем же начальным значением даёт те же переходы."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_training import _collect_transitions
    from aipackaging_ml.training import configure_determinism

    configure_determinism(42)
    model = HierarchicalPolygonPolicyV1(32)
    episodes = [_expert_episode()]
    first = _collect_transitions(model, episodes, 4, torch.device("cpu"), 91, 2, time.monotonic() + 30)
    second = _collect_transitions(model, episodes, 4, torch.device("cpu"), 91, 2, time.monotonic() + 30)
    key = lambda item: (item.instance, item.rotation, item.position, item.reward, item.terminated, item.trace_end)
    assert [key(item) for item in first] == [key(item) for item in second]


def test_polygon_bc_overfits_one_expert_action() -> None:
    """Несколько шагов BC повышают вероятность одного фиксированного экспертного действия."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_policy import evaluate_polygon_action
    from aipackaging_ml.polygon_training_data import replay_polygon_expert_steps
    from aipackaging_ml.training import configure_determinism

    configure_determinism(42)
    sample = next(replay_polygon_expert_steps(_expert_episode()))
    model = HierarchicalPolygonPolicyV1(32)
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-2)
    initial = float(
        evaluate_polygon_action(
            model, sample.environment, sample.fixed, sample.dynamic, sample.action_index, torch.device("cpu")
        ).log_probability.detach()
    )
    for _ in range(8):
        optimizer.zero_grad(set_to_none=True)
        decision = evaluate_polygon_action(
            model, sample.environment, sample.fixed, sample.dynamic, sample.action_index, torch.device("cpu")
        )
        (-decision.log_probability).backward()
        optimizer.step()
    final = float(
        evaluate_polygon_action(
            model, sample.environment, sample.fixed, sample.dynamic, sample.action_index, torch.device("cpu")
        ).log_probability.detach()
    )
    assert final > initial
