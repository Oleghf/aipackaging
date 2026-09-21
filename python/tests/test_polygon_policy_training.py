"""Проверки наблюдений, модели и контрактов полигонального обучения M6.2."""

from __future__ import annotations

import json
import random
import time
from pathlib import Path

import numpy as np
import pytest
from jsonschema import Draft202012Validator

from aipackaging_ml.environment import PolygonNestingEnv
from aipackaging_ml import _aipackaging_solver as _native
from aipackaging_ml.datasets.serialization import canonical_json
from aipackaging_ml.datasets.serialization import sha256_file, write_canonical_json
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


def test_polygon_checkpoint_restores_rng_when_loaded_to_cuda(tmp_path: Path) -> None:
    """Загрузка на CUDA оставляет состояние основного генератора на CPU."""

    torch = pytest.importorskip("torch")
    if not torch.cuda.is_available():
        pytest.skip("CUDA недоступна")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_training import load_polygon_checkpoint, save_polygon_checkpoint

    source = HierarchicalPolygonPolicyV1(32)
    optimizer = torch.optim.AdamW(source.parameters(), lr=1e-3)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    path = tmp_path / "cuda-checkpoint.pt"
    save_polygon_checkpoint(path, source, optimizer, scheduler, stage="ppo", step=1, config={"seed": 42})
    target = HierarchicalPolygonPolicyV1(32).to("cuda")
    payload = load_polygon_checkpoint(path, target, torch.device("cuda"), restore_rng=True)
    assert payload["torchRandomState"].device.type == "cuda"
    assert torch.get_rng_state().device.type == "cpu"


def test_polygon_training_budget_is_cumulative(monkeypatch: pytest.MonkeyPatch) -> None:
    """Продолжение получает только остаток общего бюджета, а не новый полный срок."""

    from aipackaging_ml import polygon_training

    now = 1000.0
    monkeypatch.setattr(polygon_training.time, "monotonic", lambda: now)
    budget = polygon_training.PolygonTrainingBudget(28_800.0, 21_600.0, now)
    assert budget.deadline == 8200.0
    now += 3600.0
    assert budget.elapsed_seconds() == 25_200.0
    assert budget.checkpoint_state({"bestScore": [1.0]})["elapsedTrainingSeconds"] == 25_200.0

    assert polygon_training._resume_elapsed_seconds(
        {"trainingState": {"elapsedTrainingSeconds": 21_600.0}}, 28_800.0
    ) == 21_600.0
    with pytest.raises(ValueError, match="не содержит накопленное время"):
        polygon_training._resume_elapsed_seconds({"trainingState": {}}, 28_800.0)
    with pytest.raises(ValueError, match="уже исчерпан"):
        polygon_training._resume_elapsed_seconds(
            {"trainingState": {"elapsedTrainingSeconds": 28_800.0}}, 28_800.0
        )


def test_finalize_polygon_run_does_not_change_weights(tmp_path: Path) -> None:
    """Финализация проверяет файлы, выбирает лучшую точку и не меняет её байты."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml import polygon_run
    from aipackaging_ml.polygon_evaluation import verify_polygon_run
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_training import save_polygon_checkpoint

    dataset = tmp_path / "dataset"
    run = tmp_path / "run"
    dataset.mkdir()
    run.mkdir()
    write_canonical_json(dataset / "manifest.json", {"fixture": True})
    config = load_polygon_training_config(ROOT / "configs" / "m6" / "polygon-policy-v1.json")
    config["datasetManifestSha256"] = sha256_file(dataset / "manifest.json")
    config_path = tmp_path / "config.json"
    write_canonical_json(config_path, config)

    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"])
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    save_polygon_checkpoint(
        run / "polygon-bc-best.pt", model, optimizer, scheduler, stage="bc", step=2, config=config,
        training_state={"bestNll": 1.0},
    )
    save_polygon_checkpoint(
        run / "polygon-ppo-best.pt", model, optimizer, scheduler, stage="ppo", step=12, config=config,
        training_state={"bestScore": [64.0, 6.0, -100.0]},
    )
    selected_hash = sha256_file(run / "polygon-ppo-best.pt")
    save_polygon_checkpoint(
        run / "polygon-resume.pt", model, optimizer, scheduler, stage="ppo", step=25, config=config,
        training_state={"bestScore": [64.0, 6.0, -100.0]},
    )
    (run / "polygon-observation-cache.zip").write_bytes(b"cache")
    manifest = polygon_run.finalize_polygon_run(config_path, dataset, run)
    assert manifest["status"] == "budget_exhausted"
    assert manifest["ppoCheckpoint"]["sha256"] == selected_hash
    assert sha256_file(run / "polygon-ppo-best.pt") == selected_hash
    assert verify_polygon_run(run) == manifest

    summary = json.loads((run / "training-summary.json").read_text(encoding="utf-8"))
    schema = json.loads(
        (ROOT / "schemas" / "polygon-training-summary-v1.schema.json").read_text(encoding="utf-8")
    )
    Draft202012Validator(schema).validate(summary)
    assert summary["elapsedTrainingSeconds"] == 28_800.0
    assert summary["selectedCheckpoint"]["step"] == 12
    assert summary["latestCheckpoint"]["step"] == 25

    summary["elapsedTrainingSeconds"] = 1.0
    write_canonical_json(run / "training-summary.json", summary)
    with pytest.raises(ValueError, match="контрольная сумма"):
        verify_polygon_run(run)


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


@pytest.mark.parametrize("count, workers", [(3, 2), (5, 4), (4, 4)])
def test_polygon_ppo_collection_uses_reproducible_worker_processes(count: int, workers: int) -> None:
    """Неполный последний такт даёт точное число и воспроизводимый порядок переходов."""

    torch = pytest.importorskip("torch")
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1
    from aipackaging_ml.polygon_training import _collect_transitions
    from aipackaging_ml.training import configure_determinism

    configure_determinism(42)
    model = HierarchicalPolygonPolicyV1(32)
    episodes = [_expert_episode()]
    first = _collect_transitions(model, episodes, count, torch.device("cpu"), 91, workers, time.monotonic() + 60)
    second = _collect_transitions(model, episodes, count, torch.device("cpu"), 91, workers, time.monotonic() + 60)
    key = lambda item: (item.instance, item.rotation, item.position, item.reward, item.terminated, item.trace_end)
    assert len(first) == len(second) == count
    assert [key(item) for item in first] == [key(item) for item in second]


def test_polygon_ppo_deadline_rolls_back_partial_update(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Истечение срока после шага оптимизатора оставляет только согласованную контрольную точку."""

    from types import SimpleNamespace

    import torch
    from aipackaging_ml import polygon_training
    from aipackaging_ml.polygon_model import HierarchicalPolygonPolicyV1

    model = HierarchicalPolygonPolicyV1(32)
    original = {name: value.clone() for name, value in model.state_dict().items()}
    transition = polygon_training.PolygonPpoTransition({}, {}, {}, 0, 0, 0, 0.0, 0.0, 0.1, 0.0, True)
    monkeypatch.setattr(polygon_training, "_collect_transitions", lambda *_: [transition] * 65)
    calls = 0

    def fake_decision(current_model: HierarchicalPolygonPolicyV1, *_: object) -> SimpleNamespace:
        """Создаёт дешёвый дифференцируемый выбор и переводит часы за предел после 65-го шага."""

        nonlocal calls
        calls += 1
        weight = next(current_model.parameters()).reshape(-1)[0]
        return SimpleNamespace(log_probability=weight, value=weight, entropy=weight * 0)

    monkeypatch.setattr(polygon_training, "evaluate_polygon_components", fake_decision)
    monkeypatch.setattr(polygon_training.time, "monotonic", lambda: 10.0 if calls >= 65 else 0.0)
    config = {"seed": 42, "ppo": {"learningRate": 1e-3, "updates": 1, "transitionsPerUpdate": 65,
                                   "epochsPerUpdate": 1, "workers": 1, "gamma": 1.0, "gaeLambda": 0.95,
                                   "entropyStart": 0.01, "entropyEnd": 0.001, "clipRatio": 0.2,
                                   "valueCoefficient": 0.5, "maxGradientNorm": 0.5}}
    budget = polygon_training.PolygonTrainingBudget(5.0, 0.0, 0.0)
    _, history, status = polygon_training.train_polygon_ppo(
        model, [], [], {}, config, tmp_path, torch.device("cpu"), smoke=False, deadline=5.0, budget=budget,
    )
    assert status == "budget_exhausted"
    assert history == []
    assert all(torch.equal(value, original[name]) for name, value in model.state_dict().items())
    payload = polygon_training.load_polygon_checkpoint(tmp_path / "polygon-resume.pt", model, torch.device("cpu"))
    assert payload["step"] == 0
    assert payload["trainingState"]["elapsedTrainingSeconds"] == 5.0


def test_polygon_bc_deadline_rolls_back_epoch_and_resumes_exactly(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch,
) -> None:
    """После прерывания BC повторяет целую эпоху и получает состояние непрерывного запуска."""

    from types import SimpleNamespace

    import torch
    from aipackaging_ml import polygon_training

    samples = [SimpleNamespace(fixed={}, dynamic={}, placement={}, instance=0, rotation=0,
                               position=0, value_target=0.0) for _ in range(4)]
    config = {"seed": 42, "behavioralCloning": {"learningRate": 1e-3, "weightDecay": 0.0,
                                                  "maxEpochs": 1, "gradientAccumulation": 2,
                                                  "earlyStoppingPatience": 10}}
    calls = 0

    def decision(model: torch.nn.Module, *_: object) -> SimpleNamespace:
        """Возвращает простую дифференцируемую оценку и считает обработанные примеры."""

        nonlocal calls
        calls += 1
        weight = next(model.parameters()).reshape(-1)[0]
        return SimpleNamespace(log_probability=weight, value=weight)

    monkeypatch.setattr(polygon_training, "evaluate_polygon_components", decision)
    monkeypatch.setattr(polygon_training, "_validation_nll", lambda *_: 1.0)
    monkeypatch.setattr(polygon_training.time, "monotonic", lambda: 0.0)
    continuous = tmp_path / "continuous"
    interrupted = tmp_path / "interrupted"
    continuous.mkdir(); interrupted.mkdir()
    random.seed(42)
    np.random.seed(42)
    torch.manual_seed(42)
    full_model = torch.nn.Linear(1, 1)
    _, full_history, full_status = polygon_training.train_polygon_bc(
        full_model, samples, samples, config, continuous, torch.device("cpu"), smoke=False, deadline=5.0,
    )
    assert full_status == "complete"
    full_checkpoint = polygon_training.load_polygon_checkpoint(
        continuous / "polygon-resume.pt", full_model, torch.device("cpu"),
    )

    random.seed(42)
    np.random.seed(42)
    torch.manual_seed(42)
    resumed_model = torch.nn.Linear(1, 1)
    calls = 0
    monkeypatch.setattr(polygon_training.time, "monotonic", lambda: 10.0 if calls >= 3 else 0.0)
    _, _, interrupted_status = polygon_training.train_polygon_bc(
        resumed_model, samples, samples, config, interrupted, torch.device("cpu"), smoke=False, deadline=5.0,
    )
    assert interrupted_status == "budget_exhausted"
    stopped = polygon_training.load_polygon_checkpoint(
        interrupted / "polygon-resume.pt", resumed_model, torch.device("cpu"),
    )
    assert stopped["step"] == stopped["trainingState"]["bcCommittedEpoch"] == 0
    monkeypatch.setattr(polygon_training.time, "monotonic", lambda: 0.0)
    _, resumed_history, resumed_status = polygon_training.train_polygon_bc(
        resumed_model, samples, samples, config, interrupted, torch.device("cpu"), smoke=False, deadline=5.0,
        resume=interrupted / "polygon-resume.pt",
    )
    assert resumed_status == "complete"
    assert resumed_history == full_history
    resumed_checkpoint = polygon_training.load_polygon_checkpoint(
        interrupted / "polygon-resume.pt", resumed_model, torch.device("cpu"),
    )
    for name, value in full_checkpoint["modelState"].items():
        assert torch.equal(value, resumed_checkpoint["modelState"][name])
    assert full_checkpoint["optimizerState"]["state"].keys() == resumed_checkpoint["optimizerState"]["state"].keys()
    for index, state in full_checkpoint["optimizerState"]["state"].items():
        for key, value in state.items():
            other = resumed_checkpoint["optimizerState"]["state"][index][key]
            assert torch.equal(value, other) if torch.is_tensor(value) else value == other
    assert full_checkpoint["schedulerState"] == resumed_checkpoint["schedulerState"]
    assert torch.equal(full_checkpoint["torchRandomState"], resumed_checkpoint["torchRandomState"])
    assert full_checkpoint["pythonRandomState"] == resumed_checkpoint["pythonRandomState"]
    assert full_checkpoint["numpyRandomState"][0] == resumed_checkpoint["numpyRandomState"][0]
    assert np.array_equal(full_checkpoint["numpyRandomState"][1], resumed_checkpoint["numpyRandomState"][1])
    assert full_checkpoint["numpyRandomState"][2:] == resumed_checkpoint["numpyRandomState"][2:]


def test_polygon_bc_rejects_legacy_ambiguous_resume(tmp_path: Path) -> None:
    """Старая контрольная точка BC читается, но не допускается к автоматическому продолжению."""

    import torch
    from aipackaging_ml import polygon_training

    model = torch.nn.Linear(1, 1)
    optimizer = torch.optim.AdamW(model.parameters())
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    checkpoint = tmp_path / "legacy.pt"
    polygon_training.save_polygon_checkpoint(
        checkpoint, model, optimizer, scheduler, stage="bc", step=0,
        config={"seed": 42, "behavioralCloning": {"learningRate": 1e-3, "weightDecay": 0.0,
                                                   "maxEpochs": 1, "gradientAccumulation": 2,
                                                   "earlyStoppingPatience": 10}},
        training_state={"bestNll": float("inf"), "stale": 0},
    )
    assert polygon_training.load_polygon_checkpoint(checkpoint, model, torch.device("cpu"))["stage"] == "bc"
    with pytest.raises(ValueError, match="не подтверждает завершённую эпоху"):
        polygon_training.train_polygon_bc(
            model, [], [], {"seed": 42, "behavioralCloning": {"learningRate": 1e-3, "weightDecay": 0.0,
                                                               "maxEpochs": 1, "gradientAccumulation": 2,
                                                               "earlyStoppingPatience": 10}},
            tmp_path, torch.device("cpu"), smoke=False, deadline=float("inf"), resume=checkpoint,
        )


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
