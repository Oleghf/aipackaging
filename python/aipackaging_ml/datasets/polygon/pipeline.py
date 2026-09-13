"""Сборка воспроизводимого рабочего полигонального набора данных v2 и его кэша."""

from __future__ import annotations

import hashlib
import multiprocessing
from collections import Counter
from pathlib import Path
from typing import Any, Mapping, Sequence

from ... import _aipackaging_solver as _native
from ..cache import read_compatible_cache_record, write_cache_record
from ..serialization import canonical_json, sha256_file, write_canonical_json, write_jsonl_gzip
from .family import polygon_family_hash, problem_features, validate_problem_profile
from .generation import (
    POLYGON_GENERATOR_REVISION,
    POLYGON_GENERATOR_VERSION,
    POLYGON_PROFILES,
    POLYGON_TIERS,
    derive_seed,
    generate_family_variant,
)
from .recipes import POLYGON_FEATURES
from .replay import verify_replay
from .rollout import POLYGON_BUDGETS, POLYGON_SMOKE_BUDGETS, POLYGON_SOLVERS, best_trajectory, rollout_problem

POLYGON_SPLITS = {"train": 256, "validation": 64, "test": 64}
POLYGON_SMOKE_SPLITS = {"train": 4, "validation": 4, "test": 4}


def _task_identity(tier: str, split: str, family_index: int, variant: int) -> dict[str, Any]:
    """Формирует устойчивый адрес одного масштабного варианта для записи кэша."""

    return {"tier": tier, "split": split, "familyIndex": family_index, "scaleVariant": variant}


def _task_key(identity: Mapping[str, Any]) -> str:
    """Кодирует адрес задачи в стабильный внутренний ключ и имя файла кэша."""

    return f"{identity['tier']}:{identity['split']}:{identity['familyIndex']}:{identity['scaleVariant']}"


def _verify_expert(problem: Mapping[str, Any], trajectories: list[dict[str, Any]], expert_id: str) -> None:
    """Повторяет действия и проверяет выбор полного экспертного результата общим компаратором."""

    if len(trajectories) != len(POLYGON_SOLVERS) or {
        item["solver"]["name"] for item in trajectories
    } != set(POLYGON_SOLVERS):
        raise ValueError(f"набор траекторий базовых алгоритмов не совпадает: {problem['problemId']}")
    for trajectory in trajectories:
        verify_replay(problem, trajectory)
    solved = [item for item in trajectories if item["finalSolution"]["status"] == "solved"]
    if not solved:
        raise ValueError(f"ни один базовый алгоритм не решил задачу набора данных: {problem['problemId']}")
    if best_trajectory(solved)["trajectoryId"] != expert_id:
        raise ValueError(f"неверное значение expertTrajectoryId: {problem['problemId']}")


def _build_task(arguments: tuple[Any, ...]) -> dict[str, Any]:
    """Выполняет детерминированный цикл отбраковки одного варианта семейства."""

    master_seed, tier, split, family_index, variant, budgets, max_attempts = arguments
    identity = _task_identity(tier, split, family_index, variant)
    last_error = "hidden layout does not fit"
    for attempt in range(max_attempts):
        problem, hidden, seed = generate_family_variant(
            master_seed, tier, split, family_index, variant, attempt
        )
        if hidden is None:
            continue
        try:
            validate_problem_profile(problem, tier)
            hidden_metrics = _native.validate_hidden_polygon_layout(canonical_json(problem), hidden)
            utilization = float(hidden_metrics["materialUtilization"])
            if not 0.0 < utilization <= 1.0:
                last_error = f"hidden utilization {utilization} is invalid"
                continue
            trajectories, expert = rollout_problem(problem, seed, budgets, require_solved=True)
        except (RuntimeError, ValueError) as error:
            last_error = str(error)
            continue
        return {
            "problem": problem,
            "tier": tier,
            "split": split,
            "familyIndex": family_index,
            "scaleVariant": variant,
            "derivedSeed": seed,
            "familyHash": polygon_family_hash(problem),
            "attempt": attempt,
            "features": problem_features(problem),
            "trajectories": trajectories,
            "expertTrajectoryId": expert,
        }
    raise RuntimeError(f"не удалось создать {tier}/{split}/{family_index}/{variant}: {last_error}")


def _validate_generation_arguments(
    split_sizes: Mapping[str, int],
    tiers: Sequence[str],
    workers: int,
    budgets: Mapping[str, int],
    max_attempts: int,
) -> None:
    """Проверяет баланс профилей и семейств, число процессов, бюджеты и предел попыток."""

    if set(split_sizes) != {"train", "validation", "test"}:
        raise ValueError("`split_sizes` должен содержать обучающую, проверочную и тестовую выборки")
    if not tiers or any(tier not in POLYGON_TIERS for tier in tiers) or len(set(tiers)) != len(tiers):
        raise ValueError("`tiers` должен быть непустым уникальным подмножеством `small` и `medium`")
    divisor = 2 * len(tiers)
    if any(
        isinstance(size, bool) or not isinstance(size, int) or size < 0 or size % divisor != 0
        for size in split_sizes.values()
    ):
        raise ValueError(f"размер каждой выборки должен быть неотрицательным и делиться на {divisor}")
    if isinstance(workers, bool) or not isinstance(workers, int) or workers < 1:
        raise ValueError("число рабочих процессов должно быть положительным")
    if set(budgets) != {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"}:
        raise ValueError("набор полей бюджета полигонального решателя не совпадает")
    if isinstance(budgets["timeoutMs"], bool) or not isinstance(budgets["timeoutMs"], int):
        raise ValueError("некорректные бюджеты полигонального решателя")
    if budgets["timeoutMs"] != 0 or any(
        isinstance(budgets[name], bool) or not isinstance(budgets[name], int) or budgets[name] < 1
        for name in ("randomIterations", "beamWidth", "maxExpandedStates")
    ):
        raise ValueError("некорректные бюджеты полигонального решателя")
    if (
        isinstance(max_attempts, bool)
        or not isinstance(max_attempts, int)
        or not 1 <= max_attempts <= 256
    ):
        raise ValueError("`max_attempts` должен находиться в диапазоне от 1 до 256")


def _generation_fingerprint(
    master_seed: int,
    tiers: Sequence[str],
    split_sizes: Mapping[str, int],
    budgets: Mapping[str, int],
    max_attempts: int,
) -> str:
    """Вычисляет отпечаток конфигурации, влияющей на содержимое кэша."""

    value = {
        "generatorVersion": POLYGON_GENERATOR_VERSION,
        "generatorRevision": POLYGON_GENERATOR_REVISION,
        "projectVersion": _native.__version__,
        "nativeRevision": _native.__revision__,
        "masterSeed": master_seed,
        "tiers": list(tiers),
        "splitSizes": dict(split_sizes),
        "solverBudgets": dict(budgets),
        "maxAttempts": max_attempts,
    }
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def _validate_cached_payload(
    payload: Mapping[str, Any], identity: Mapping[str, Any], master_seed: int, max_attempts: int
) -> None:
    """Полностью перепроверяет совместимое содержимое кэша перед использованием."""

    expected_fields = {
        "problem", "tier", "split", "familyIndex", "scaleVariant", "derivedSeed", "familyHash",
        "attempt", "features", "trajectories", "expertTrajectoryId",
    }
    if set(payload) != expected_fields:
        raise ValueError("набор полей содержимого кэша возобновления не совпадает")
    for name, expected in identity.items():
        if payload[name] != expected:
            raise ValueError("идентификатор содержимого кэша возобновления не совпадает")
    attempt = payload["attempt"]
    if isinstance(attempt, bool) or not isinstance(attempt, int) or not 0 <= attempt < max_attempts:
        raise ValueError("номер попытки в кэше возобновления не совпадает")
    expected_seed = derive_seed(
        master_seed,
        "polygon-task-v2",
        identity["tier"],
        identity["split"],
        identity["familyIndex"],
        identity["scaleVariant"],
        attempt,
    )
    problem = payload["problem"]
    if payload["derivedSeed"] != expected_seed:
        raise ValueError("производное начальное значение в кэше возобновления не совпадает")
    if payload["familyHash"] != polygon_family_hash(problem) or payload["features"] != problem_features(problem):
        raise ValueError("производные метаданные геометрии в кэше возобновления не совпадают")
    validate_problem_profile(problem, identity["tier"])
    _verify_expert(problem, payload["trajectories"], payload["expertTrajectoryId"])


def generate_polygon_dataset(
    output: str | Path,
    *,
    master_seed: int = 42,
    split_sizes: Mapping[str, int] = POLYGON_SPLITS,
    tiers: Sequence[str] = POLYGON_TIERS,
    workers: int = 1,
    budgets: Mapping[str, int] = POLYGON_BUDGETS,
    max_attempts: int = 256,
    resume: bool = False,
    mode: str = "canonical",
) -> dict[str, Any]:
    """Создаёт возобновляемый `polygon_dataset` v2 и возвращает его манифест."""

    if isinstance(master_seed, bool) or not isinstance(master_seed, int) or master_seed < 0:
        raise ValueError("`master_seed` должен быть неотрицательным целым числом")
    if mode not in {"canonical", "smoke", "custom"}:
        raise ValueError("поле `mode` должно задавать канонический, пробный или пользовательский режим")
    if set(split_sizes) != {"train", "validation", "test"}:
        raise ValueError("`split_sizes` должен содержать обучающую, проверочную и тестовую выборки")
    if set(budgets) != {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"}:
        raise ValueError("набор полей бюджета полигонального решателя не совпадает")
    tiers = tuple(tiers)
    split_sizes = {name: split_sizes[name] for name in ("train", "validation", "test")}
    budgets = {name: budgets[name] for name in (
        "timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"
    )}
    _validate_generation_arguments(split_sizes, tiers, workers, budgets, max_attempts)
    fingerprint = _generation_fingerprint(master_seed, tiers, split_sizes, budgets, max_attempts)
    root = Path(output)
    cache_root = root / ".work"

    task_arguments: list[tuple[Any, ...]] = []
    for split in ("train", "validation", "test"):
        families_per_tier = split_sizes[split] // (2 * len(tiers))
        for tier in tiers:
            for family_index in range(families_per_tier):
                for variant in range(2):
                    task_arguments.append((
                        master_seed, tier, split, family_index, variant, dict(budgets), max_attempts
                    ))

    results: dict[str, dict[str, Any]] = {}
    missing = []
    for arguments in task_arguments:
        _, tier, split, family_index, variant, _, _ = arguments
        identity = _task_identity(tier, split, family_index, variant)
        key = _task_key(identity)
        cache_path = cache_root / f"{tier}-{split}-{family_index:04d}-{variant}.json"
        if resume and cache_path.exists():
            try:
                payload = read_compatible_cache_record(
                    cache_path, expected_identity=identity, expected_fingerprint=fingerprint
                )
                if payload is not None:
                    _validate_cached_payload(payload, identity, master_seed, max_attempts)
                    results[key] = payload
                    continue
            except (KeyError, TypeError, ValueError) as error:
                raise ValueError(f"некорректный кэш возобновления: {cache_path}: {error}") from error
        missing.append(arguments)

    if workers == 1:
        generated = map(_build_task, missing)
        for arguments, payload in zip(missing, generated, strict=True):
            identity = _task_identity(arguments[1], arguments[2], arguments[3], arguments[4])
            results[_task_key(identity)] = payload
            write_cache_record(
                cache_root / f"{arguments[1]}-{arguments[2]}-{arguments[3]:04d}-{arguments[4]}.json",
                identity=identity,
                fingerprint=fingerprint,
                payload=payload,
            )
    else:
        with multiprocessing.get_context("spawn").Pool(workers) as pool:
            # Неупорядоченная выдача уменьшает простой рабочих процессов; итог ниже
    # собирается в порядке аргументов задач.
            for payload in pool.imap_unordered(_build_task, missing):
                identity = _task_identity(
                    payload["tier"], payload["split"], payload["familyIndex"], payload["scaleVariant"]
                )
                results[_task_key(identity)] = payload
                write_cache_record(
                    cache_root / (
                        f"{identity['tier']}-{identity['split']}-{identity['familyIndex']:04d}-"
                        f"{identity['scaleVariant']}.json"
                    ),
                    identity=identity,
                    fingerprint=fingerprint,
                    payload=payload,
                )

    ordered = []
    for arguments in task_arguments:
        identity = _task_identity(arguments[1], arguments[2], arguments[3], arguments[4])
        ordered.append(results[_task_key(identity)])
    family_hashes: dict[tuple[str, str, int], set[str]] = {}
    for payload in ordered:
        family_key = (payload["tier"], payload["split"], payload["familyIndex"])
        family_hashes.setdefault(family_key, set()).add(payload["familyHash"])
    if any(len(values) != 1 for values in family_hashes.values()):
        raise RuntimeError("масштабные варианты дали разные хеши семейства")

    shards = []
    experts: dict[str, str] = {}
    metadata: dict[str, dict[str, Any]] = {}
    coverage = {tier: Counter() for tier in tiers}
    revisions: set[str] = set()
    for split in ("train", "validation", "test"):
        selected = [payload for payload in ordered if payload["split"] == split]
        problems = [payload["problem"] for payload in selected]
        trajectories = [trajectory for payload in selected for trajectory in payload["trajectories"]]
        for payload in selected:
            problem_id = payload["problem"]["problemId"]
            experts[problem_id] = payload["expertTrajectoryId"]
            metadata[problem_id] = {
                name: payload[name] for name in (
                    "tier", "familyIndex", "scaleVariant", "derivedSeed", "familyHash", "attempt", "features"
                )
            }
            coverage[payload["tier"]].update(payload["features"])
        revisions.update(trajectory["solver"]["revision"] for trajectory in trajectories)
        for kind, path, records in (
            ("problems", root / f"{split}-problems.jsonl.gz", problems),
            ("trajectories", root / f"{split}-trajectories.jsonl.gz", trajectories),
        ):
            write_jsonl_gzip(path, records)
            shards.append({
                "split": split,
                "kind": kind,
                "path": path.relative_to(root).as_posix(),
                "records": len(records),
                "sha256": sha256_file(path),
            })
    if len(revisions) > 1:
        raise RuntimeError("полигональные траектории содержат разные ревизии сборки")
    if mode == "canonical" and any(set(values) != set(POLYGON_FEATURES) for values in coverage.values()):
        raise RuntimeError("канонический полигональный набор не покрывает все признаки в каждом профиле")

    manifest = {
        "format": "aipackaging.polygon_dataset",
        "version": 2,
        "problemContractVersion": 1,
        "trajectoryContractVersion": 1,
        "observationVersion": 1,
        "masterSeed": master_seed,
        "revision": next(iter(revisions), "unknown"),
        "generator": {
            "name": "deterministic-polygon-tiers",
            "version": POLYGON_GENERATOR_VERSION,
            "implementationRevision": POLYGON_GENERATOR_REVISION,
            "mode": mode,
            "tiers": {tier: POLYGON_PROFILES[tier] for tier in tiers},
            "splitSizes": split_sizes,
            "variantsPerFamily": 2,
            "maxAttempts": max_attempts,
        },
        "solverBudgets": budgets,
        "problems": dict(sorted(metadata.items())),
        "coverage": {tier: dict(sorted(values.items())) for tier, values in coverage.items()},
        "shards": shards,
        "expertTrajectoryId": dict(sorted(experts.items())),
    }
    write_canonical_json(root / "manifest.json", manifest)
    return manifest
