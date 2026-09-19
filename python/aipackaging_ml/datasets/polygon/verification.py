"""Строгая проверка полигональных наборов данных v1/v2 и динамических траекторий."""

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
    """Проверяет описатели, хеши, канонические записи и уникальность идентификаторов."""

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
            raise ValueError(f"некорректный вид части набора: {shard['path']}")
        descriptor = (shard["split"], shard["kind"])
        if descriptor in descriptors or shard["path"] in paths:
            raise ValueError(f"повторяющийся описатель части полигонального набора: {shard['path']}")
        descriptors.add(descriptor)
        paths.add(shard["path"])
        shard_path = resolve_dataset_path(root, shard["path"])
        if sha256_file(shard_path) != shard["sha256"]:
            raise ValueError(f"некорректная часть набора: {shard['path']}")
        records = read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"количество записей не совпадает: {shard['path']}")
        target = problems if shard["kind"] == "problems" else trajectories
        key = "problemId" if shard["kind"] == "problems" else "trajectoryId"
        for record in records:
            if key not in record:
                raise ValueError(f"отсутствует {key}: {shard['path']}")
            if record[key] in target:
                raise ValueError(f"повторяется {key}: {record[key]}")
            target[record[key]] = record
            if shard["kind"] == "problems":
                problem_splits[record[key]] = shard["split"]
            else:
                trajectory_splits[record[key]] = shard["split"]

    expected = {(split, kind) for split in ("train", "validation", "test") for kind in ("problems", "trajectories")}
    if descriptors != expected:
        raise ValueError("полигональный набор должен объявлять части всех выборок и видов")
    for trajectory_id, trajectory in trajectories.items():
        if trajectory.get("problemId") not in problems:
            raise ValueError(f"неизвестный problemId траектории: {trajectory_id}")
        if problem_splits[trajectory["problemId"]] != trajectory_splits[trajectory_id]:
            raise ValueError(f"траектория сохранена в неверной выборке: {trajectory_id}")
    return problems, trajectories, problem_splits, trajectory_splits


def _verify_experts(
    experts: Mapping[str, str],
    problems: Mapping[str, Mapping[str, Any]],
    trajectories: Mapping[str, Mapping[str, Any]],
    *,
    require_solved: bool,
) -> None:
    """Проверяет пять базовых алгоритмов задачи и повторяет выбор лучшего результата компаратором C++."""

    if set(experts) != set(problems):
        raise ValueError("набор ключей expertTrajectoryId не совпадает")
    for problem_id, expert_id in experts.items():
        candidates = [item for item in trajectories.values() if item["problemId"] == problem_id]
        if len(candidates) != len(POLYGON_SOLVERS) or {
            item["solver"]["name"] for item in candidates
        } != set(POLYGON_SOLVERS):
            raise ValueError(f"набор траекторий базовых алгоритмов не совпадает: {problem_id}")
        solved = [item for item in candidates if item["finalSolution"]["status"] == "solved"]
        if require_solved and not solved:
            raise ValueError(f"ни один базовый алгоритм не решил задачу набора: {problem_id}")
        eligible = solved if solved else candidates
        if best_trajectory(eligible)["trajectoryId"] != expert_id:
            raise ValueError(f"неверное значение expertTrajectoryId: {problem_id}")


def _verify_replay_all(
    problems: Mapping[str, Mapping[str, Any]], trajectories: Mapping[str, Mapping[str, Any]]
) -> None:
    """Воспроизводит каждую траекторию через нативное динамическое пространство действий."""

    for trajectory in trajectories.values():
        verify_replay(problems[trajectory["problemId"]], trajectory)


def _verify_v1(root: Path, manifest: Mapping[str, Any]) -> dict[str, int]:
    """Сохраняет строгую проверку опубликованного пробного `polygon_dataset` v1."""

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
            raise ValueError(f"семейство полигонов попало в разные выборки: {problem_id}")
    _verify_replay_all(problems, trajectories)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories, require_solved=False)
    return {"problems": len(problems), "trajectories": len(trajectories)}


def _verify_v2(root: Path, manifest: Mapping[str, Any]) -> dict[str, int]:
    """Проверяет профили, метаданные, изоляцию семейств, покрытие и повтор данных v2."""

    require_keys(manifest, {
        "format", "version", "problemContractVersion", "trajectoryContractVersion", "observationVersion",
        "masterSeed", "revision", "generator", "solverBudgets", "problems", "coverage", "shards",
        "expertTrajectoryId",
    }, "manifest")
    if any(manifest[name] != 1 for name in (
        "problemContractVersion", "trajectoryContractVersion", "observationVersion"
    )):
        raise ValueError("неподдерживаемая версия компонента полигонального набора")
    if isinstance(manifest["masterSeed"], bool) or not isinstance(manifest["masterSeed"], int) or manifest["masterSeed"] < 0:
        raise ValueError("некорректное главное начальное значение полигонального набора")
    if not isinstance(manifest["revision"], str) or not manifest["revision"]:
        raise ValueError("некорректная ревизия полигонального набора")
    generator = manifest["generator"]
    require_keys(generator, {
        "name", "version", "implementationRevision", "mode", "tiers", "splitSizes", "variantsPerFamily",
        "maxAttempts",
    }, "generator")
    if generator["name"] != "deterministic-polygon-tiers" or generator["version"] != 2:
        raise ValueError("неподдерживаемый генератор полигонального набора")
    if generator["implementationRevision"] != POLYGON_GENERATOR_REVISION:
        raise ValueError("неподдерживаемая ревизия генератора полигонального набора")
    if generator["mode"] not in {"canonical", "smoke", "custom"}:
        raise ValueError("некорректный режим генератора полигонального набора")
    if (
        generator["variantsPerFamily"] != 2
        or isinstance(generator["maxAttempts"], bool)
        or not isinstance(generator["maxAttempts"], int)
        or not 1 <= generator["maxAttempts"] <= 256
    ):
        raise ValueError("некорректные параметры семейства полигонов или цикла отбраковки")
    declared_tiers = generator["tiers"]
    if not isinstance(declared_tiers, Mapping) or not declared_tiers:
        raise ValueError("полигональный набор должен объявлять хотя бы один профиль сложности")
    if any(tier not in POLYGON_PROFILES or profile != POLYGON_PROFILES[tier] for tier, profile in declared_tiers.items()):
        raise ValueError("профили сложности полигонов не соответствуют контракту v2")
    split_sizes = require_keys(generator["splitSizes"], {"train", "validation", "test"}, "split sizes")
    divisor = 2 * len(declared_tiers)
    if any(
        isinstance(size, bool) or not isinstance(size, int) or size < 0 or size % divisor != 0
        for size in split_sizes.values()
    ):
        raise ValueError("некорректные размеры выборок полигонального набора")
    budgets = require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )
    if budgets["timeoutMs"] != 0 or any(
        isinstance(budgets[name], bool) or not isinstance(budgets[name], int) or budgets[name] < 1
        for name in ("randomIterations", "beamWidth", "maxExpandedStates")
    ):
        raise ValueError("некорректные замороженные бюджеты решателей")

    problems, trajectories, problem_splits, _ = _load_shards(root, manifest)
    if not isinstance(manifest["problems"], Mapping) or set(manifest["problems"]) != set(problems):
        raise ValueError("набор ключей метаданных задачи не совпадает")
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
            raise ValueError(f"задача ссылается на необъявленный профиль: {problem_id}")
        actual_hash = polygon_family_hash(problem)
        if metadata["familyHash"] != actual_hash:
            raise ValueError(f"хеш семейства не совпадает: {problem_id}")
        owner = family_splits.setdefault(actual_hash, problem_splits[problem_id])
        if owner != problem_splits[problem_id]:
            raise ValueError(f"семейство полигонов попало в разные выборки: {problem_id}")
        actual_features = problem_features(problem)
        if metadata["features"] != actual_features:
            raise ValueError(f"метаданные признаков не совпадают: {problem_id}")
        coverage[tier].update(actual_features)
        validate_problem_profile(problem, tier)

        family_index = metadata["familyIndex"]
        variant = metadata["scaleVariant"]
        attempt = metadata["attempt"]
        if isinstance(family_index, bool) or not isinstance(family_index, int) or family_index < 0:
            raise ValueError(f"некорректный индекс семейства полигонов: {problem_id}")
        if isinstance(variant, bool) or not isinstance(variant, int) or variant not in (0, 1):
            raise ValueError(f"некорректный масштабный вариант полигона: {problem_id}")
        if isinstance(attempt, bool) or not isinstance(attempt, int) or not 0 <= attempt < generator["maxAttempts"]:
            raise ValueError(f"некорректный номер попытки отбраковки: {problem_id}")
        if isinstance(metadata["derivedSeed"], bool) or not isinstance(metadata["derivedSeed"], int):
            raise ValueError(f"некорректное производное начальное значение: {problem_id}")
        expected_seed = derive_seed(
            manifest["masterSeed"], "polygon-task-v2", tier, problem_splits[problem_id], family_index, variant, attempt
        )
        if metadata["derivedSeed"] != expected_seed:
            raise ValueError(f"производное начальное значение не совпадает: {problem_id}")
        expected_id = (
            f"polygon-v2-{tier}-{problem_splits[problem_id]}-f{family_index:04d}-v{variant}-{expected_seed:016x}"
        )
        if problem_id != expected_id:
            raise ValueError(f"идентификатор задачи не соответствует метаданным: {problem_id}")
        key = (problem_splits[problem_id], tier, family_index)
        variants = family_variants.setdefault(key, {})
        if variant in variants:
            raise ValueError(f"повторяющийся вариант семейства полигонов: {problem_id}")
        variants[variant] = actual_hash

    if any(set(variants) != {0, 1} or len(set(variants.values())) != 1 for variants in family_variants.values()):
        raise ValueError("масштабные варианты семейства полигонов неполны или несогласованны")
    actual_coverage = {tier: dict(sorted(values.items())) for tier, values in coverage.items()}
    if manifest["coverage"] != actual_coverage:
        raise ValueError("покрытие не соответствует задачам")
    if generator["mode"] == "canonical" and any(set(values) != set(POLYGON_FEATURES) for values in coverage.values()):
        raise ValueError("каноническое покрытие признаков полигонов неполно")
    actual_counts = Counter(problem_splits.values())
    if any(actual_counts[split] != split_sizes[split] for split in ("train", "validation", "test")):
        raise ValueError("размеры выборок не соответствуют частям набора")
    for trajectory in trajectories.values():
        solver = trajectory["solver"]
        problem_metadata = manifest["problems"][trajectory["problemId"]]
        if solver["revision"] != manifest["revision"] or solver["seed"] != problem_metadata["derivedSeed"]:
            raise ValueError(f"ревизия или начальное значение траектории не совпадает: {trajectory['trajectoryId']}")
        if any(
            solver[name] != budgets[budget_name]
            for name, budget_name in (
                ("randomIterations", "randomIterations"),
                ("beamWidth", "beamWidth"),
                ("maxExpandedStates", "maxExpandedStates"),
                ("timeoutMs", "timeoutMs"),
            )
        ):
            raise ValueError(f"бюджеты траектории не совпадают: {trajectory['trajectoryId']}")
    _verify_replay_all(problems, trajectories)
    _verify_experts(manifest["expertTrajectoryId"], problems, trajectories, require_solved=True)
    return {"problems": len(problems), "trajectories": len(trajectories), "version": 2}


def verify_polygon_dataset(path: str | Path) -> dict[str, int]:
    """Автоматически проверяет совместимый `polygon_dataset` v1 либо v2."""

    root = Path(path)
    manifest = read_canonical_json(root / "manifest.json")
    if manifest.get("format") != "aipackaging.polygon_dataset":
        raise ValueError("неподдерживаемый манифест `polygon_dataset`")
    if manifest.get("version") == 1:
        return _verify_v1(root, manifest)
    if manifest.get("version") == 2:
        return _verify_v2(root, manifest)
    raise ValueError("неподдерживаемая версия `polygon_dataset`")


def load_polygon_dataset_records(
    path: str | Path, split: str
) -> tuple[dict[str, Any], list[dict[str, Any]], list[dict[str, Any]]]:
    """Возвращает проверенные манифест, задачи и траектории выбранной выборки."""

    if split not in {"train", "validation", "test"}:
        raise ValueError("неизвестная выборка полигонального набора данных")
    root = Path(path)
    verify_polygon_dataset(root)
    manifest = read_canonical_json(root / "manifest.json")
    by_kind = {
        shard["kind"]: shard for shard in manifest["shards"] if shard["split"] == split
    }
    problems = read_jsonl_gzip(resolve_dataset_path(root, by_kind["problems"]["path"]))
    trajectories = read_jsonl_gzip(resolve_dataset_path(root, by_kind["trajectories"]["path"]))
    return manifest, problems, trajectories
