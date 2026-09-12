"""Строгая проверка polygon datasets v1/v2 и динамических trajectories."""

from __future__ import annotations

import hashlib
from collections import Counter
from pathlib import Path
from typing import Any, Mapping

from ..serialization import (
    canonical_json,
    read_canonical_json,
    read_jsonl_gzip,
    require_keys,
    resolve_dataset_path,
    sha256_file,
)
from .family import polygon_family_hash, problem_features, validate_problem_profile
from .generation import POLYGON_GENERATOR_REVISION, POLYGON_PROFILES, derive_seed
from .recipes import POLYGON_FEATURES
from .replay import verify_replay
from .rollout import POLYGON_SOLVERS, best_trajectory


def _load_shards(
    root: Path, manifest: Mapping[str, Any]
) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]], dict[str, str], dict[str, str]]:
    """Проверяет descriptors, hashes, canonical records и уникальность идентификаторов."""

    problems: dict[str, dict[str, Any]] = {}
    trajectories: dict[str, dict[str, Any]] = {}
    problem_splits: dict[str, str] = {}
    trajectory_splits: dict[str, str] = {}
    descriptors: set[tuple[str, str]] = set()
    paths: set[str] = set()
    for shard in manifest["shards"]:
        require_keys(shard, {"split", "kind", "path", "records", "sha256"}, "shard")
        if shard["split"] not in {"train", "validation", "test"} or shard["kind"] not in {
            "problems", "trajectories"
        }:
            raise ValueError(f"invalid shard discriminator: {shard['path']}")
        descriptor = (shard["split"], shard["kind"])
        if descriptor in descriptors or shard["path"] in paths:
            raise ValueError(f"duplicate polygon shard descriptor: {shard['path']}")
        descriptors.add(descriptor)
        paths.add(shard["path"])
        shard_path = resolve_dataset_path(root, shard["path"])
        if sha256_file(shard_path) != shard["sha256"]:
            raise ValueError(f"invalid shard: {shard['path']}")
        records = read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"record count mismatch: {shard['path']}")
        target = problems if shard["kind"] == "problems" else trajectories
        key = "problemId" if shard["kind"] == "problems" else "trajectoryId"
        for record in records:
            if key not in record:
                raise ValueError(f"missing {key}: {shard['path']}")
            if record[key] in target:
                raise ValueError(f"duplicate {key}: {record[key]}")
            target[record[key]] = record
            if shard["kind"] == "problems":
                problem_splits[record[key]] = shard["split"]
            else:
                trajectory_splits[record[key]] = shard["split"]

    expected = {(split, kind) for split in ("train", "validation", "test") for kind in ("problems", "trajectories")}
    if descriptors != expected:
        raise ValueError("polygon dataset must declare every split/kind shard")
    for trajectory_id, trajectory in trajectories.items():
        if trajectory.get("problemId") not in problems:
            raise ValueError(f"unknown trajectory problemId: {trajectory_id}")
        if problem_splits[trajectory["problemId"]] != trajectory_splits[trajectory_id]:
            raise ValueError(f"trajectory is stored in the wrong split: {trajectory_id}")
    return problems, trajectories, problem_splits, trajectory_splits


def _verify_experts(
    experts: Mapping[str, str],
    problems: Mapping[str, Mapping[str, Any]],
    trajectories: Mapping[str, Mapping[str, Any]],
    *,
    require_solved: bool,
) -> None:
    """Проверяет пять baseline на задачу и повторяет выбор expert C++-компаратором."""

    if set(experts) != set(problems):
        raise ValueError("expertTrajectoryId keys mismatch")
    for problem_id, expert_id in experts.items():
        candidates = [item for item in trajectories.values() if item["problemId"] == problem_id]
        if len(candidates) != len(POLYGON_SOLVERS) or {
            item["solver"]["name"] for item in candidates
        } != set(POLYGON_SOLVERS):
            raise ValueError(f"baseline trajectory set mismatch: {problem_id}")
        solved = [item for item in candidates if item["finalSolution"]["status"] == "solved"]
        if require_solved and not solved:
            raise ValueError(f"dataset task has no solved baseline: {problem_id}")
        eligible = solved if solved else candidates
        if best_trajectory(eligible)["trajectoryId"] != expert_id:
            raise ValueError(f"wrong expertTrajectoryId: {problem_id}")


def _verify_replay_all(
    problems: Mapping[str, Mapping[str, Any]], trajectories: Mapping[str, Mapping[str, Any]]
) -> None:
    """Воспроизводит каждую trajectory через native dynamic action space."""

    for trajectory in trajectories.values():
        verify_replay(problems[trajectory["problemId"]], trajectory)


def _verify_v1(root: Path, manifest: Mapping[str, Any]) -> dict[str, int]:
    """Сохраняет строгую проверку опубликованного smoke polygon_dataset v1."""

    require_keys(manifest, {
        "format", "version", "problemContractVersion", "trajectoryContractVersion", "observationVersion",
        "masterSeed", "revision", "generator", "solverBudgets", "shards", "expertTrajectoryId",
    }, "manifest")
    require_keys(manifest["generator"], {"name", "version", "splitSizes"}, "generator")
    require_keys(manifest["generator"]["splitSizes"], {"train", "validation", "test"}, "split sizes")
    require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )
    problems, trajectories, problem_splits, _ = _load_shards(root, manifest)
    family_splits: dict[str, str] = {}
    for problem_id, problem in problems.items():
        source = {"sheet": problem["sheet"], "manufacturing": problem["manufacturing"], "parts": problem["parts"]}
        signature = hashlib.sha256(canonical_json(source).encode("utf-8")).hexdigest()
        owner = family_splits.setdefault(signature, problem_splits[problem_id])
        if owner != problem_splits[problem_id]:
            raise ValueError(f"polygon family split leakage: {problem_id}")
    _verify_replay_all(problems, trajectories)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories, require_solved=False)
    return {"problems": len(problems), "trajectories": len(trajectories)}


def _verify_v2(root: Path, manifest: Mapping[str, Any]) -> dict[str, int]:
    """Проверяет tiers, metadata, family isolation, coverage и replay dataset v2."""

    require_keys(manifest, {
        "format", "version", "problemContractVersion", "trajectoryContractVersion", "observationVersion",
        "masterSeed", "revision", "generator", "solverBudgets", "problems", "coverage", "shards",
        "expertTrajectoryId",
    }, "manifest")
    if any(manifest[name] != 1 for name in (
        "problemContractVersion", "trajectoryContractVersion", "observationVersion"
    )):
        raise ValueError("unsupported polygon dataset component version")
    if isinstance(manifest["masterSeed"], bool) or not isinstance(manifest["masterSeed"], int) or manifest["masterSeed"] < 0:
        raise ValueError("invalid polygon dataset master seed")
    if not isinstance(manifest["revision"], str) or not manifest["revision"]:
        raise ValueError("invalid polygon dataset revision")
    generator = manifest["generator"]
    require_keys(generator, {
        "name", "version", "implementationRevision", "mode", "tiers", "splitSizes", "variantsPerFamily",
        "maxAttempts",
    }, "generator")
    if generator["name"] != "deterministic-polygon-tiers" or generator["version"] != 2:
        raise ValueError("unsupported polygon dataset generator")
    if generator["implementationRevision"] != POLYGON_GENERATOR_REVISION:
        raise ValueError("unsupported polygon dataset generator revision")
    if generator["mode"] not in {"canonical", "smoke", "custom"}:
        raise ValueError("invalid polygon dataset generator mode")
    if (
        generator["variantsPerFamily"] != 2
        or isinstance(generator["maxAttempts"], bool)
        or not isinstance(generator["maxAttempts"], int)
        or not 1 <= generator["maxAttempts"] <= 256
    ):
        raise ValueError("invalid polygon family or rejection-loop parameters")
    declared_tiers = generator["tiers"]
    if not isinstance(declared_tiers, Mapping) or not declared_tiers:
        raise ValueError("polygon dataset must declare at least one tier")
    if any(tier not in POLYGON_PROFILES or profile != POLYGON_PROFILES[tier] for tier, profile in declared_tiers.items()):
        raise ValueError("polygon tier profiles do not match contract v2")
    split_sizes = require_keys(generator["splitSizes"], {"train", "validation", "test"}, "split sizes")
    divisor = 2 * len(declared_tiers)
    if any(
        isinstance(size, bool) or not isinstance(size, int) or size < 0 or size % divisor != 0
        for size in split_sizes.values()
    ):
        raise ValueError("invalid polygon dataset split sizes")
    budgets = require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )
    if budgets["timeoutMs"] != 0 or any(
        isinstance(budgets[name], bool) or not isinstance(budgets[name], int) or budgets[name] < 1
        for name in ("randomIterations", "beamWidth", "maxExpandedStates")
    ):
        raise ValueError("invalid frozen solver budgets")

    problems, trajectories, problem_splits, _ = _load_shards(root, manifest)
    if not isinstance(manifest["problems"], Mapping) or set(manifest["problems"]) != set(problems):
        raise ValueError("problem metadata keys mismatch")
    family_splits: dict[str, str] = {}
    family_variants: dict[tuple[str, str, int], dict[int, str]] = {}
    coverage = {tier: Counter() for tier in declared_tiers}
    for problem_id, problem in problems.items():
        metadata = manifest["problems"][problem_id]
        require_keys(metadata, {
            "tier", "familyIndex", "scaleVariant", "derivedSeed", "familyHash", "attempt", "features",
        }, "problem metadata")
        tier = metadata["tier"]
        if tier not in declared_tiers:
            raise ValueError(f"problem references undeclared tier: {problem_id}")
        actual_hash = polygon_family_hash(problem)
        if metadata["familyHash"] != actual_hash:
            raise ValueError(f"family hash mismatch: {problem_id}")
        owner = family_splits.setdefault(actual_hash, problem_splits[problem_id])
        if owner != problem_splits[problem_id]:
            raise ValueError(f"polygon family split leakage: {problem_id}")
        actual_features = problem_features(problem)
        if metadata["features"] != actual_features:
            raise ValueError(f"feature metadata mismatch: {problem_id}")
        coverage[tier].update(actual_features)
        validate_problem_profile(problem, tier)

        family_index = metadata["familyIndex"]
        variant = metadata["scaleVariant"]
        attempt = metadata["attempt"]
        if isinstance(family_index, bool) or not isinstance(family_index, int) or family_index < 0:
            raise ValueError(f"invalid polygon family index: {problem_id}")
        if isinstance(variant, bool) or not isinstance(variant, int) or variant not in (0, 1):
            raise ValueError(f"invalid polygon scale variant: {problem_id}")
        if isinstance(attempt, bool) or not isinstance(attempt, int) or not 0 <= attempt < generator["maxAttempts"]:
            raise ValueError(f"invalid rejection attempt: {problem_id}")
        if isinstance(metadata["derivedSeed"], bool) or not isinstance(metadata["derivedSeed"], int):
            raise ValueError(f"invalid derived seed: {problem_id}")
        expected_seed = derive_seed(
            manifest["masterSeed"], "polygon-task-v2", tier, problem_splits[problem_id], family_index, variant, attempt
        )
        if metadata["derivedSeed"] != expected_seed:
            raise ValueError(f"derived seed mismatch: {problem_id}")
        expected_id = (
            f"polygon-v2-{tier}-{problem_splits[problem_id]}-f{family_index:04d}-v{variant}-{expected_seed:016x}"
        )
        if problem_id != expected_id:
            raise ValueError(f"problem id does not match metadata: {problem_id}")
        key = (problem_splits[problem_id], tier, family_index)
        variants = family_variants.setdefault(key, {})
        if variant in variants:
            raise ValueError(f"duplicate polygon family variant: {problem_id}")
        variants[variant] = actual_hash

    if any(set(variants) != {0, 1} or len(set(variants.values())) != 1 for variants in family_variants.values()):
        raise ValueError("polygon family scale variants are incomplete or inconsistent")
    actual_coverage = {tier: dict(sorted(values.items())) for tier, values in coverage.items()}
    if manifest["coverage"] != actual_coverage:
        raise ValueError("coverage does not match problems")
    if generator["mode"] == "canonical" and any(set(values) != set(POLYGON_FEATURES) for values in coverage.values()):
        raise ValueError("canonical polygon feature coverage is incomplete")
    actual_counts = Counter(problem_splits.values())
    if any(actual_counts[split] != split_sizes[split] for split in ("train", "validation", "test")):
        raise ValueError("split sizes do not match shards")
    for trajectory in trajectories.values():
        solver = trajectory["solver"]
        problem_metadata = manifest["problems"][trajectory["problemId"]]
        if solver["revision"] != manifest["revision"] or solver["seed"] != problem_metadata["derivedSeed"]:
            raise ValueError(f"trajectory revision or seed mismatch: {trajectory['trajectoryId']}")
        if any(
            solver[name] != budgets[budget_name]
            for name, budget_name in (
                ("randomIterations", "randomIterations"),
                ("beamWidth", "beamWidth"),
                ("maxExpandedStates", "maxExpandedStates"),
                ("timeoutMs", "timeoutMs"),
            )
        ):
            raise ValueError(f"trajectory budgets mismatch: {trajectory['trajectoryId']}")
    _verify_replay_all(problems, trajectories)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories, require_solved=True)
    return {"problems": len(problems), "trajectories": len(trajectories), "version": 2}


def verify_polygon_dataset(path: str | Path) -> dict[str, int]:
    """Автоматически проверяет совместимый polygon_dataset v1 либо v2."""

    root = Path(path)
    manifest = read_canonical_json(root / "manifest.json")
    if manifest.get("format") != "aipackaging.polygon_dataset":
        raise ValueError("unsupported polygon_dataset manifest")
    if manifest.get("version") == 1:
        return _verify_v1(root, manifest)
    if manifest.get("version") == 2:
        return _verify_v2(root, manifest)
    raise ValueError("unsupported polygon_dataset version")


def load_polygon_dataset_records(
    path: str | Path, split: str
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    """Возвращает проверенные manifest, задачи и trajectories выбранного split."""

    if split not in {"train", "validation", "test"}:
        raise ValueError("unknown polygon dataset split")
    root = Path(path)
    verify_polygon_dataset(root)
    manifest = read_canonical_json(root / "manifest.json")
    by_kind = {
        shard["kind"]: shard for shard in manifest["shards"] if shard["split"] == split
    }
    problems = read_jsonl_gzip(resolve_dataset_path(root, by_kind["problems"]["path"]))
    trajectories = read_jsonl_gzip(resolve_dataset_path(root, by_kind["trajectories"]["path"]))
    return manifest, problems, trajectories
