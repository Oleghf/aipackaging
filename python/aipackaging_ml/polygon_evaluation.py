"""Оценка контрольной точки полигональной политики и проверка запуска."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping, Sequence

import torch

from . import _aipackaging_solver as _native
from .datasets.serialization import canonical_json, read_canonical_json, sha256_file, write_canonical_json
from .polygon_contracts import load_polygon_training_config
from .polygon_model import HierarchicalPolygonPolicyV1
from .polygon_policy import PolygonPolicyRunner
from .polygon_training import load_polygon_checkpoint
from .polygon_training_data import load_polygon_baseline_solutions, load_polygon_expert_episodes

SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")


def _summary(solutions: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    """Вычисляет полноту и средние компоненты целевой функции."""

    solved = [item for item in solutions if item["status"] == "solved"]
    denominator = max(len(solved), 1)
    return {"tasks": len(solutions), "solved": len(solved),
            "meanUsedLengthMicrometers": sum(item["objective"]["usedLengthMicrometers"] for item in solved) / denominator,
            "meanPrimaryRemnantWidthMicrometers": sum(item["objective"]["primaryRemnantWidthMicrometers"] for item in solved) / denominator,
            "meanLargestExtraRectangleSquareMicrometers": sum(item["objective"]["largestExtraRectangleSquareMicrometers"] for item in solved) / denominator,
            "meanFragmentationPenaltySquareMicrometers": sum(item["objective"]["fragmentationPenaltySquareMicrometers"] for item in solved) / denominator}


def evaluate_polygon_checkpoint(checkpoint: str | Path, config_path: str | Path, dataset_root: str | Path,
                                output_path: str | Path, *, split: str = "validation", device_name: str = "cpu",
                                smoke: bool = False) -> dict[str, Any]:
    """Сравнивает политику с пятью замороженными базовыми алгоритмами."""

    if split not in {"validation", "test"}:
        raise ValueError("оценка разрешена только на проверочной или тестовой выборке")
    config = load_polygon_training_config(config_path)
    episodes = load_polygon_expert_episodes(dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"])
    baselines = load_polygon_baseline_solutions(dataset_root, split, expected_manifest_sha256=config["datasetManifestSha256"])
    if smoke:
        episodes = episodes[:1]
    device = torch.device(device_name)
    model = HierarchicalPolygonPolicyV1(config["model"]["hiddenSize"]).to(device)
    checkpoint_hash = sha256_file(checkpoint)
    load_polygon_checkpoint(checkpoint, model, device, expected_sha256=checkpoint_hash)
    runner = PolygonPolicyRunner(model, model_id="hierarchical-polygon-policy-v1", model_sha256=checkpoint_hash, device=device,
                                 revision=_native.__revision__)
    rollouts = 2 if smoke else config["evaluation"]["neuralRollouts"]
    collected: dict[str, list[dict[str, Any]]] = {name: [] for name in (*SOLVERS, "neural-greedy", "neural-best-of", "hybrid")}
    comparisons = []
    wins = ties = losses = used_wins = 0
    for index, episode in enumerate(episodes):
        problem_id = episode.problem["problemId"]
        for solver in SOLVERS:
            collected[solver].append(baselines[problem_id][solver])
        greedy = runner.solve(episode.problem, mode="greedy", seed=config["seed"] + index)
        best = runner.solve(episode.problem, mode="best-of", rollouts=rollouts, seed=config["seed"] + index * rollouts)
        reference = baselines[problem_id]["random-left-bottom"]
        hybrid = runner.hybrid_solution(
            episode.problem,
            best,
            reference,
            seed=config["seed"] + index * rollouts,
            rollouts=rollouts,
        )
        collected["neural-greedy"].append(greedy); collected["neural-best-of"].append(best); collected["hybrid"].append(hybrid)
        better = _native.is_better_polygon_solution(canonical_json(hybrid), canonical_json(reference))
        worse = _native.is_better_polygon_solution(canonical_json(reference), canonical_json(hybrid))
        result = "win" if better else "loss" if worse else "tie"
        wins += result == "win"; losses += result == "loss"; ties += result == "tie"
        delta = reference["objective"]["usedLengthMicrometers"] - hybrid["objective"]["usedLengthMicrometers"]
        used_wins += delta > 0
        comparisons.append({"problemId": problem_id, "result": result, "usedLengthImprovementMicrometers": delta,
                            "hybridComplete": hybrid["status"] == "solved"})
    complete = all(item["status"] == "solved" for item in collected["hybrid"])
    passed = complete and losses == 0 and wins >= config["evaluation"]["requiredLexicographicWins"] and used_wins >= config["evaluation"]["requiredUsedLengthWins"]
    report = {"format": "aipackaging.polygon_evaluation_report", "version": 1, "split": split,
              "seed": config["seed"], "datasetManifestSha256": config["datasetManifestSha256"],
              "checkpointSha256": checkpoint_hash, "rollouts": rollouts,
              "summaries": {name: _summary(values) for name, values in collected.items()},
              "hybridVsRandom": {"wins": wins, "ties": ties, "losses": losses, "usedLengthWins": used_wins,
                                 "allComplete": complete},
              "qualityGatePassed": passed, "comparisons": comparisons}
    write_canonical_json(output_path, report)
    return report


def verify_polygon_run(run_dir: str | Path) -> dict[str, Any]:
    """Проверяет формат и SHA-256 всех файлов завершённого или прерванного запуска."""

    root = Path(run_dir)
    manifest = read_canonical_json(root / "run-manifest.json")
    expected = {"format", "version", "status", "seed", "device", "datasetManifestSha256", "configSha256",
                "bcCheckpoint", "ppoCheckpoint", "policy", "metrics", "observationCache", "software"}
    if set(manifest) != expected or manifest["format"] != "aipackaging.polygon_training_run" or manifest["version"] != 1:
        raise ValueError("неподдерживаемый манифест полигонального обучения")
    for field in ("bcCheckpoint", "ppoCheckpoint", "policy", "metrics", "observationCache"):
        descriptor = manifest[field]
        if set(descriptor) != {"path", "sha256"} or Path(descriptor["path"]).name != descriptor["path"]:
            raise ValueError(f"некорректное описание файла запуска: {field}")
        if sha256_file(root / descriptor["path"]) != descriptor["sha256"]:
            raise ValueError(f"контрольная сумма файла запуска не совпадает: {field}")
        if field in {"bcCheckpoint", "ppoCheckpoint"}:
            sidecar = root / (descriptor["path"] + ".sha256")
            if not sidecar.is_file() or sidecar.read_text(encoding="ascii").strip() != descriptor["sha256"]:
                raise ValueError(f"файл SHA-256 контрольной точки не совпадает: {field}")
    return manifest
