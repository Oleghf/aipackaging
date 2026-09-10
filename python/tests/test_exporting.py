"""Проверка checkpoint/resume и двухчастного ONNX bundle M3."""

from __future__ import annotations

import random
from pathlib import Path

import numpy as np
import pytest

torch = pytest.importorskip("torch")
onnx = pytest.importorskip("onnx")
pytest.importorskip("onnxruntime")

from aipackaging_ml.contracts import load_training_config
from aipackaging_ml.environment import GridNestingEnv
from aipackaging_ml.exporting import export_policy_bundle
from aipackaging_ml.model import HierarchicalGridPolicyV1
from aipackaging_ml.model_bundle import load_model_metadata
from aipackaging_ml.onnx_policy import OnnxGreedyPolicy
from aipackaging_ml.policy import select_action
from aipackaging_ml.training import configure_determinism, load_checkpoint, save_checkpoint

ROOT = Path(__file__).parents[2]
CONFIG = ROOT / "configs" / "m3" / "grid-policy-v1.json"
FIXTURE = Path(__file__).parent / "fixtures" / "smoke-problem.json"


def _checkpoint(path: Path) -> tuple[HierarchicalGridPolicyV1, torch.optim.Optimizer, torch.optim.lr_scheduler.LRScheduler]:
    """Создаёт минимальный trusted checkpoint с optimizer и scheduler state."""

    config = load_training_config(CONFIG)
    model = HierarchicalGridPolicyV1(config["model"]["hiddenSize"])
    optimizer = torch.optim.AdamW(model.parameters(), lr=1.0e-4)
    scheduler = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda _: 1.0)
    save_checkpoint(path, model, optimizer, scheduler, stage="ppo", step=7, config=config)
    return model, optimizer, scheduler


def test_checkpoint_restores_model_scheduler_and_all_cpu_rng(tmp_path: Path) -> None:
    """Resume возвращает веса, шаг scheduler и последовательности трёх RNG."""

    configure_determinism(731)
    checkpoint = tmp_path / "checkpoint.pt"
    model, optimizer, scheduler = _checkpoint(checkpoint)
    expected_parameter = next(model.parameters()).detach().clone()
    expected_random = (random.random(), float(np.random.random()), float(torch.rand(())))

    with torch.no_grad():
        next(model.parameters()).add_(1.0)
    optimizer.step()
    scheduler.step()
    random.random()
    np.random.random()
    torch.rand(())

    payload = load_checkpoint(
        checkpoint,
        model,
        torch.device("cpu"),
        optimizer=optimizer,
        scheduler=scheduler,
        restore_rng=True,
    )
    actual_random = (random.random(), float(np.random.random()), float(torch.rand(())))
    assert payload["step"] == 7
    assert scheduler.last_epoch == payload["schedulerState"]["last_epoch"]
    assert torch.equal(next(model.parameters()), expected_parameter)
    assert actual_random == pytest.approx(expected_random)


def test_onnx_bundle_matches_pytorch_greedy_action_and_detects_corruption(tmp_path: Path) -> None:
    """Оба ONNX-графа повторяют PyTorch action и защищены общим SHA-256."""

    checkpoint = tmp_path / "checkpoint.pt"
    model, _, _ = _checkpoint(checkpoint)
    bundle = tmp_path / "model"
    metadata = export_policy_bundle(checkpoint, CONFIG, bundle, model_id="fixture-policy")
    assert load_model_metadata(bundle) == metadata
    assert not list(bundle.glob("*.data"))
    assert onnx.load(bundle / "encoder.onnx").opset_import[0].version == 23
    assert onnx.load(bundle / "placement-head.onnx").opset_import[0].version == 23

    environment = GridNestingEnv.from_file(FIXTURE)
    fixed = environment.static_observation()
    dynamic, _ = environment.reset_compact()
    expected = select_action(model.eval(), fixed, dynamic, torch.device("cpu")).action_index
    actual = OnnxGreedyPolicy(bundle).action(fixed, dynamic)
    assert actual == expected
    assert dynamic["action_mask"][actual]

    with (bundle / "placement-head.onnx").open("ab") as stream:
        stream.write(b"corrupt")
    with pytest.raises(ValueError, match="checksum"):
        load_model_metadata(bundle)
