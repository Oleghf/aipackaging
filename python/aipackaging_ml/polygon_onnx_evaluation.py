"""Проверка численной совместимости и качества полигонального комплекта ONNX."""

from __future__ import annotations

from pathlib import Path
from typing import Any

import numpy as np
import torch

from . import _aipackaging_solver as _native
from .datasets.serialization import canonical_json, write_canonical_json
from .environment import PolygonNestingEnv
from .polygon_contracts import load_polygon_training_config
from .polygon_evaluation import SOLVERS, _summary
from .polygon_exporting import load_polygon_model_metadata
from .polygon_model import HierarchicalPolygonPolicyV1
from .polygon_onnx import OnnxPolygonPolicy, OnnxPolygonPolicyRunner
from .polygon_policy import encode_polygon_observation, select_polygon_action
from .polygon_training import load_polygon_checkpoint
from .polygon_training_data import load_polygon_baseline_solutions, load_polygon_expert_episodes


def verify_polygon_model_bundle(
    model_root: str | Path,
    checkpoint: str | Path,
    config_path: str | Path,
    dataset_root: str | Path,
    *,
    smoke: bool = False,
) -> dict[str, Any]:
    """Сравнивает оценки и жадные действия PyTorch и ONNX на проверочной выборке."""

    metadata = load_polygon_model_metadata(model_root)
    config = load_polygon_training_config(config_path)
    if metadata["datasetManifestSha256"] != config["datasetManifestSha256"]:
        raise ValueError("комплект ONNX и конфигурация ссылаются на разные наборы данных")
    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"])
    load_polygon_checkpoint(checkpoint, model, torch.device("cpu"), expected_sha256=metadata["checkpointSha256"])
    model.eval()
    onnx = OnnxPolygonPolicy(model_root)
    episodes = load_polygon_expert_episodes(
        dataset_root, "validation", expected_manifest_sha256=config["datasetManifestSha256"]
    )
    if smoke:
        episodes = episodes[:1]
    checked = 0
    maximum_error = 0.0
    for episode in episodes:
        environment = PolygonNestingEnv.from_dict(episode.problem, reward_version=2, catalog_version=1)
        fixed = environment.static_observation()
        dynamic, _ = environment.reset_compact(seed=config["seed"])
        while not environment.is_terminal:
            with torch.no_grad():
                encoded = encode_polygon_observation(model, fixed, dynamic, torch.device("cpu"))
                outputs = onnx.encode(fixed, dynamic)
                expected = (
                    encoded.state_embedding.numpy(), encoded.orientation_embeddings.numpy(),
                    encoded.instance_logits.numpy(), encoded.rotation_logits.numpy(), encoded.value.numpy(),
                )
                for left, right in zip(expected, outputs, strict=True):
                    maximum_error = max(maximum_error, float(np.max(np.abs(left - right))))
                torch_action = select_polygon_action(
                    model, environment, fixed, dynamic, torch.device("cpu"), generator=None
                ).action_index
            onnx_action = onnx.action(environment, fixed, dynamic)
            if torch_action != onnx_action:
                raise ValueError(f"жадное действие ONNX не совпало: {episode.problem['problemId']}, шаг {checked}")
            if maximum_error > 1.0e-5:
                raise ValueError(f"расхождение вычислений ONNX превысило 0,00001: {maximum_error}")
            dynamic, _, _, _, _ = environment.step_compact(onnx_action)
            checked += 1
    return {"modelSha256": metadata["modelSha256"], "checkedActions": checked, "maximumLogitError": maximum_error}


def evaluate_polygon_onnx(
    model_root: str | Path,
    config_path: str | Path,
    dataset_root: str | Path,
    output_path: str | Path,
    *,
    split: str = "validation",
    smoke: bool = False,
) -> dict[str, Any]:
    """Сравнивает ONNX-политику с замороженными базовыми решениями M6.1."""

    if split not in {"validation", "test"}:
        raise ValueError("оценка разрешена только на проверочной или тестовой выборке")
    config = load_polygon_training_config(config_path)
    model = OnnxPolygonPolicy(model_root)
    metadata = model.metadata
    if metadata["datasetManifestSha256"] != config["datasetManifestSha256"]:
        raise ValueError("комплект ONNX не соответствует выбранному набору данных")
    runner = OnnxPolygonPolicyRunner(model, revision=_native.__revision__)
    episodes = load_polygon_expert_episodes(
        dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"]
    )
    baselines = load_polygon_baseline_solutions(
        dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"]
    )
    if smoke:
        episodes = episodes[:1]
    rollouts = 2 if smoke else config["evaluation"]["neuralRollouts"]
    collected: dict[str, list[dict[str, Any]]] = {
        name: [] for name in (*SOLVERS, "neural-greedy", "neural-best-of", "hybrid")
    }
    comparisons: list[dict[str, Any]] = []
    wins = ties = losses = used_wins = 0
    for index, episode in enumerate(episodes):
        problem_id = episode.problem["problemId"]
        for solver in SOLVERS:
            collected[solver].append(baselines[problem_id][solver])
        seed = config["seed"] + index * rollouts
        greedy = runner.solve(episode.problem, mode="greedy", seed=config["seed"] + index)
        best = runner.solve(episode.problem, mode="best-of", rollouts=rollouts, seed=seed)
        reference = baselines[problem_id]["random-left-bottom"]
        hybrid = runner.hybrid_solution(episode.problem, best, reference, seed=seed, rollouts=rollouts)
        collected["neural-greedy"].append(greedy)
        collected["neural-best-of"].append(best)
        collected["hybrid"].append(hybrid)
        better = _native.is_better_polygon_solution(canonical_json(hybrid), canonical_json(reference))
        worse = _native.is_better_polygon_solution(canonical_json(reference), canonical_json(hybrid))
        result = "win" if better else "loss" if worse else "tie"
        wins += result == "win"
        losses += result == "loss"
        ties += result == "tie"
        delta = reference["objective"]["usedLengthMicrometers"] - hybrid["objective"]["usedLengthMicrometers"]
        used_wins += delta > 0
        comparisons.append({
            "problemId": problem_id, "result": result, "usedLengthImprovementMicrometers": delta,
            "hybridComplete": hybrid["status"] == "solved",
        })
    complete = all(item["status"] == "solved" for item in collected["hybrid"])
    passed = (
        complete and losses == 0 and wins >= config["evaluation"]["requiredLexicographicWins"]
        and used_wins >= config["evaluation"]["requiredUsedLengthWins"]
    )
    report = {
        "format": "aipackaging.polygon_evaluation_report", "version": 1, "split": split,
        "seed": config["seed"], "datasetManifestSha256": config["datasetManifestSha256"],
        "checkpointSha256": metadata["checkpointSha256"], "rollouts": rollouts,
        "summaries": {name: _summary(values) for name, values in collected.items()},
        "hybridVsRandom": {"wins": wins, "ties": ties, "losses": losses, "usedLengthWins": used_wins,
                           "allComplete": complete},
        "qualityGatePassed": passed, "comparisons": comparisons,
    }
    write_canonical_json(output_path, report)
    return report
