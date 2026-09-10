"""Замороженная оценка M3 и проверка PyTorch/ONNX parity."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping, Sequence

import torch

from . import _aipackaging_solver as _native
from .contracts import canonical_json, load_training_config, sha256_file, write_canonical_json
from .environment import GridNestingEnv
from .model import HierarchicalGridPolicyV1
from .model_bundle import compare_arrays, load_model_metadata
from .onnx_policy import OnnxGreedyPolicy
from .policy import PolicyRunner, encode_observation, select_action
from .training import load_checkpoint
from .training_data import load_expert_episodes, load_frozen_baseline_solutions

BASELINES = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")


def _aggregate(solutions: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    """Агрегирует полноту и objective-компоненты без смешивания с wall-clock."""

    solved = [solution for solution in solutions if solution["status"] == "solved"]
    denominator = max(len(solved), 1)
    return {
        "tasks": len(solutions),
        "solved": len(solved),
        "meanUsedLength": sum(solution["objective"]["usedLength"] for solution in solved) / denominator,
        "meanPrimaryRemnantWidth": sum(solution["objective"]["primaryRemnantWidth"] for solution in solved) / denominator,
        "meanLargestExtraRectangleArea": sum(solution["objective"]["largestExtraRectangleArea"] for solution in solved) / denominator,
        "meanFragmentationPenalty": sum(solution["objective"]["fragmentationPenalty"] for solution in solved) / denominator,
    }


def evaluate_checkpoint(
    checkpoint: str | Path,
    config_path: str | Path,
    dataset_root: str | Path,
    output: str | Path,
    *,
    split: str = "validation",
    device_name: str = "cpu",
    smoke: bool = False,
) -> dict[str, Any]:
    """Сравнивает пять baseline, neural greedy/best-of и безопасный hybrid."""

    config = load_training_config(config_path)
    episodes = load_expert_episodes(dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"])
    if smoke:
        episodes = episodes[:2]
    device = torch.device(device_name)
    model = HierarchicalGridPolicyV1(config["model"]["hiddenSize"]).to(device)
    load_checkpoint(checkpoint, model, device)
    checkpoint_hash = sha256_file(checkpoint)
    runner = PolicyRunner(
        model, model_id="grid-policy-v1", model_sha256=checkpoint_hash, device=device, limits=config["model"]
    )
    seed = config["seed"]
    rollout_count = 2 if smoke else config["evaluation"]["neuralRollouts"]
    frozen = load_frozen_baseline_solutions(
        dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"]
    )

    results: dict[str, list[dict[str, Any]]] = {name: [] for name in BASELINES}
    results.update({"neural-greedy": [], "neural-best-of": [], "hybrid": []})
    comparisons = []
    for episode in episodes:
        baselines = frozen[episode.problem["problemId"]]
        if set(baselines) != set(BASELINES):
            raise ValueError(f"incomplete frozen baseline set: {episode.problem['problemId']}")
        for name, solution in baselines.items():
            error = _native.validate_solution(canonical_json(episode.problem), canonical_json(solution))
            if error:
                raise RuntimeError(f"frozen baseline {name} failed validation: {error}")
        task_seed = int(baselines["random-left-bottom"]["solver"]["seed"])
        greedy = runner.solve(episode.problem, mode="greedy", seed=task_seed)
        best = runner.solve(episode.problem, mode="best-of", rollouts=rollout_count, seed=task_seed)
        hybrid = runner.solve(episode.problem, mode="hybrid", rollouts=rollout_count, seed=task_seed)
        for name, solution in baselines.items():
            results[name].append(solution)
        results["neural-greedy"].append(greedy)
        results["neural-best-of"].append(best)
        results["hybrid"].append(hybrid)

        random_solution = baselines["random-left-bottom"]
        hybrid_better = _native.is_better_solution(canonical_json(hybrid), canonical_json(random_solution))
        random_better = _native.is_better_solution(canonical_json(random_solution), canonical_json(hybrid))
        comparisons.append(
            {
                "problemId": episode.problem["problemId"],
                "result": "win" if hybrid_better else "loss" if random_better else "tie",
                "usedLengthDelta": random_solution["objective"]["usedLength"] - hybrid["objective"]["usedLength"],
            }
        )

    wins = sum(item["result"] == "win" for item in comparisons)
    losses = sum(item["result"] == "loss" for item in comparisons)
    used_length_wins = sum(item["usedLengthDelta"] > 0 for item in comparisons)
    hybrid_solved = sum(solution["status"] == "solved" for solution in results["hybrid"])
    gate = (
        split == "test"
        and len(episodes) == 128
        and hybrid_solved == len(episodes)
        and losses == 0
        and wins >= config["evaluation"]["requiredLexicographicWins"]
        and used_length_wins >= config["evaluation"]["requiredUsedLengthWins"]
    )
    report = {
        "format": "aipackaging.evaluation_report",
        "version": 1,
        "split": split,
        "seed": seed,
        "datasetManifestSha256": config["datasetManifestSha256"],
        "checkpointSha256": checkpoint_hash,
        "rollouts": rollout_count,
        "summaries": {name: _aggregate(solutions) for name, solutions in sorted(results.items())},
        "hybridVsRandom": {"wins": wins, "ties": len(comparisons) - wins - losses, "losses": losses, "usedLengthWins": used_length_wins},
        "qualityGatePassed": gate,
        "comparisons": comparisons,
    }
    write_canonical_json(output, report)
    return report


def verify_model_bundle(
    bundle: str | Path,
    checkpoint: str | Path,
    config_path: str | Path,
    dataset_root: str | Path,
    *,
    split: str = "validation",
    smoke: bool = False,
) -> dict[str, Any]:
    """Проверяет hashes и совпадение greedy action sequence PyTorch/ONNX."""

    config = load_training_config(config_path)
    metadata = load_model_metadata(bundle)
    if metadata["checkpointSha256"] != sha256_file(checkpoint):
        raise ValueError("checkpoint does not match model metadata")
    episodes = load_expert_episodes(dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"])
    if smoke:
        episodes = episodes[:2]
    model = HierarchicalGridPolicyV1(config["model"]["hiddenSize"])
    load_checkpoint(checkpoint, model, torch.device("cpu"))
    model.eval()
    onnx_policy = OnnxGreedyPolicy(bundle)
    checked_actions = 0

    for episode in episodes:
        pytorch_environment = GridNestingEnv.from_dict(episode.problem)
        onnx_environment = GridNestingEnv.from_dict(episode.problem)
        fixed = pytorch_environment.static_observation()
        pytorch_dynamic, _ = pytorch_environment.reset_compact()
        onnx_dynamic, _ = onnx_environment.reset_compact()
        with torch.no_grad():
            while not pytorch_environment.is_terminal:
                encoded = encode_observation(model, fixed, pytorch_dynamic, torch.device("cpu"))
                onnx_encoded = onnx_policy.encode(fixed, onnx_dynamic)
                for expected, actual in zip(encoded, onnx_encoded, strict=True):
                    compare_arrays(expected.detach().numpy(), actual, config["export"]["logitTolerance"])
                pytorch_action = select_action(model, fixed, pytorch_dynamic, torch.device("cpu")).action_index
                onnx_action = onnx_policy.action(fixed, onnx_dynamic)
                if pytorch_action != onnx_action:
                    raise ValueError(f"ONNX action mismatch: {episode.problem['problemId']} at step {checked_actions}")
                pytorch_dynamic, _, _, _, _ = pytorch_environment.step_compact(pytorch_action)
                onnx_dynamic, _, _, _, _ = onnx_environment.step_compact(onnx_action)
                checked_actions += 1
        if onnx_environment.is_complete != pytorch_environment.is_complete:
            raise ValueError(f"ONNX terminal mismatch: {episode.problem['problemId']}")
    return {"tasks": len(episodes), "actions": checked_actions, "modelSha256": metadata["modelSha256"]}
