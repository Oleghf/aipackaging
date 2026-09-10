"""Создание, каноническая запись и полная replay-проверка grid_dataset v1."""

from __future__ import annotations

import gzip
import hashlib
import json
import multiprocessing
from pathlib import Path
from typing import Any, Iterable, Mapping

from . import _aipackaging_solver as _native
from .environment import GridNestingEnv, canonical_json
from .generator import PROFILES, family_hash, generate_unique_problem

SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")
DEFAULT_SPLITS = {"train": 256, "validation": 64, "test": 64}


def _canonical_line(value: Mapping[str, Any]) -> bytes:
    """Кодирует одну JSONL-запись стабильным UTF-8 представлением."""

    return (canonical_json(value) + "\n").encode("utf-8")


def _write_jsonl_gzip(path: Path, records: Iterable[Mapping[str, Any]]) -> None:
    """Записывает стабильный gzip без timestamp и исходного имени файла."""

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            for record in records:
                compressed.write(_canonical_line(record))


def _read_jsonl_gzip(path: Path) -> list[dict[str, Any]]:
    """Читает весь shard и отклоняет не-объекты или повреждённый gzip/JSONL."""

    result: list[dict[str, Any]] = []
    compressed_bytes = path.read_bytes()
    if len(compressed_bytes) < 10 or compressed_bytes[4:8] != b"\x00\x00\x00\x00":
        raise ValueError(f"{path}: gzip mtime must be zero")
    with gzip.open(path, "rt", encoding="utf-8", newline="") as stream:
        for line_number, line in enumerate(stream, 1):
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{line_number}: JSONL record must be an object")
            if line.encode("utf-8") != _canonical_line(value):
                raise ValueError(f"{path}:{line_number}: JSONL record is not canonical")
            result.append(value)
    return result


def _sha256(path: Path) -> str:
    """Возвращает hex SHA-256 точных байтов файла."""

    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _require_keys(value: Mapping[str, Any], expected: set[str], label: str) -> None:
    """Отклоняет пропущенные и неизвестные поля строгого Python wire-контракта."""

    actual = set(value)
    if actual != expected:
        raise ValueError(f"{label} fields mismatch: expected {sorted(expected)}, got {sorted(actual)}")


def _best_trajectory(trajectories: list[dict[str, Any]]) -> dict[str, Any]:
    """Выбирает лучшую траекторию публичным C++-компаратором M1."""

    best = trajectories[0]
    for candidate in trajectories[1:]:
        if _native.is_better_solution(
            canonical_json(candidate["finalSolution"]), canonical_json(best["finalSolution"])
        ):
            best = candidate
    return best


def _rollout_task(task: tuple[dict[str, Any], int]) -> tuple[str, list[dict[str, Any]], str]:
    """Строит пять baseline-траекторий одной задачи в worker-процессе."""

    problem, seed = task
    problem_json = canonical_json(problem)
    trajectories: list[dict[str, Any]] = []
    for solver in SOLVERS:
        solution = json.loads(
            _native.solve_problem(
                problem_json,
                solver=solver,
                seed=seed,
                random_iterations=64,
                beam_width=32,
                max_expanded_states=50_000,
                timeout_ms=0,
            )
        )
        validation_error = _native.validate_solution(problem_json, canonical_json(solution))
        if validation_error:
            raise RuntimeError(f"{problem['problemId']}/{solver}: {validation_error}")

        environment = GridNestingEnv.from_dict(problem)
        environment.reset(seed=seed)
        steps = []
        for placement in solution["placements"]:
            action_index = environment.find_action(placement)
            action = environment.action(action_index)
            _, reward, terminated, truncated, info = environment.step(action_index)
            steps.append(
                {
                    "actionIndex": action_index,
                    "action": action,
                    "reward": reward,
                    "rankBefore": info["rankBefore"],
                    "rankAfter": info["rankAfter"],
                    "deltas": info["deltas"],
                    "terminated": terminated,
                    "truncated": truncated,
                }
            )
        trajectory_id = f"{problem['problemId']}:{solver}"
        trajectories.append(
            {
                "format": "aipackaging.grid_trajectory",
                "version": 1,
                "trajectoryId": trajectory_id,
                "problemId": problem["problemId"],
                "solver": solution["solver"],
                "actionIndices": [step["actionIndex"] for step in steps],
                "steps": steps,
                "finalSolution": solution,
            }
        )
    expert = _best_trajectory(trajectories)["trajectoryId"]
    return problem["problemId"], trajectories, expert


def generate_dataset(
    output: str | Path,
    *,
    master_seed: int = 42,
    tiers: tuple[str, ...] = ("small", "medium"),
    split_sizes: Mapping[str, int] = DEFAULT_SPLITS,
    workers: int = 1,
) -> dict[str, Any]:
    """Генерирует все shards и возвращает записанный manifest grid_dataset v1."""

    output_path = Path(output)
    output_path.mkdir(parents=True, exist_ok=True)
    family_splits: dict[str, str] = {}
    grouped: dict[tuple[str, str], list[tuple[dict[str, Any], int]]] = {}
    for tier in tiers:
        for split in ("train", "validation", "test"):
            tasks = []
            for index in range(split_sizes[split]):
                tasks.append(generate_unique_problem(master_seed, tier, split, index, family_splits))
            grouped[(tier, split)] = tasks

    ordered_tasks = [task for key in sorted(grouped) for task in grouped[key]]
    if workers == 1:
        rollout_results = [_rollout_task(task) for task in ordered_tasks]
    else:
        # spawn обеспечивает одинаковую модель процессов на Windows, Linux и macOS.
        context = multiprocessing.get_context("spawn")
        with context.Pool(processes=workers) as pool:
            rollout_results = pool.map(_rollout_task, ordered_tasks)
    rollouts = {problem_id: (trajectories, expert) for problem_id, trajectories, expert in rollout_results}

    shards = []
    experts: dict[str, str] = {}
    revision = "unknown"
    for (tier, split), tasks in sorted(grouped.items()):
        problems = [problem for problem, _ in tasks]
        trajectories = [item for problem in problems for item in rollouts[problem["problemId"]][0]]
        for problem in problems:
            experts[problem["problemId"]] = rollouts[problem["problemId"]][1]
        if trajectories:
            revision = trajectories[0]["solver"]["revision"]

        problem_path = output_path / tier / f"{split}-problems.jsonl.gz"
        trajectory_path = output_path / tier / f"{split}-trajectories.jsonl.gz"
        _write_jsonl_gzip(problem_path, problems)
        _write_jsonl_gzip(trajectory_path, trajectories)
        for kind, path, count in (
            ("problems", problem_path, len(problems)),
            ("trajectories", trajectory_path, len(trajectories)),
        ):
            shards.append(
                {
                    "tier": tier,
                    "split": split,
                    "kind": kind,
                    "path": path.relative_to(output_path).as_posix(),
                    "records": count,
                    "sha256": _sha256(path),
                }
            )

    profiles = {
        tier: {
            "sheetDimensions": [PROFILES[tier].sheet_min, PROFILES[tier].sheet_max],
            "typeCount": [PROFILES[tier].type_min, PROFILES[tier].type_max],
            "instanceCount": [PROFILES[tier].instance_min, PROFILES[tier].instance_max],
            "partArea": [PROFILES[tier].part_area_min, PROFILES[tier].part_area_max],
            "utilization": [PROFILES[tier].utilization_min, PROFILES[tier].utilization_max],
        }
        for tier in tiers
    }
    manifest = {
        "format": "aipackaging.grid_dataset",
        "version": 1,
        "problemContractVersion": 1,
        "trajectoryContractVersion": 1,
        "observationVersion": 1,
        "rewardVersion": 1,
        "masterSeed": master_seed,
        "revision": revision,
        "generator": {"name": "seeded-polyomino-growth", "version": 1, "tiers": profiles, "splitSizes": dict(split_sizes)},
        "solverBudgets": {"timeoutMs": 0, "randomIterations": 64, "beamWidth": 32, "maxExpandedStates": 50_000},
        "shards": shards,
        "expertTrajectoryId": dict(sorted(experts.items())),
    }
    (output_path / "manifest.json").write_text(canonical_json(manifest) + "\n", encoding="utf-8", newline="\n")
    return manifest


def verify_dataset(path: str | Path) -> dict[str, int]:
    """Проверяет checksums, split isolation, решения и полный replay всех действий."""

    root = Path(path)
    manifest_text = (root / "manifest.json").read_text(encoding="utf-8")
    manifest = json.loads(manifest_text)
    _require_keys(
        manifest,
        {
            "format",
            "version",
            "problemContractVersion",
            "trajectoryContractVersion",
            "observationVersion",
            "rewardVersion",
            "masterSeed",
            "revision",
            "generator",
            "solverBudgets",
            "shards",
            "expertTrajectoryId",
        },
        "manifest",
    )
    if manifest.get("format") != "aipackaging.grid_dataset" or manifest.get("version") != 1:
        raise ValueError("unsupported grid_dataset manifest")
    if manifest_text.encode("utf-8") != _canonical_line(manifest):
        raise ValueError("manifest is not canonical")
    _require_keys(manifest["generator"], {"name", "version", "tiers", "splitSizes"}, "generator")
    _require_keys(
        manifest["solverBudgets"],
        {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
        "solver budgets",
    )

    problem_records: dict[str, tuple[str, dict[str, Any]]] = {}
    trajectory_records: dict[str, dict[str, Any]] = {}
    family_splits: dict[str, str] = {}
    for shard in manifest["shards"]:
        _require_keys(shard, {"tier", "split", "kind", "path", "records", "sha256"}, "shard")
        shard_path = (root / shard["path"]).resolve()
        if root.resolve() not in shard_path.parents:
            raise ValueError(f"shard path escapes dataset root: {shard['path']}")
        if _sha256(shard_path) != shard["sha256"]:
            raise ValueError(f"checksum mismatch: {shard['path']}")
        records = _read_jsonl_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"record count mismatch: {shard['path']}")
        if shard["kind"] == "problems":
            for problem in records:
                problem_id = problem["problemId"]
                if problem_id in problem_records:
                    raise ValueError(f"duplicate problemId: {problem_id}")
                signature = family_hash(problem)
                owner = family_splits.setdefault(signature, shard["split"])
                if owner != shard["split"]:
                    raise ValueError(f"family split leakage: {problem_id}")
                problem_records[problem_id] = (shard["split"], problem)
        elif shard["kind"] == "trajectories":
            for trajectory in records:
                _require_keys(
                    trajectory,
                    {"format", "version", "trajectoryId", "problemId", "solver", "actionIndices", "steps", "finalSolution"},
                    "trajectory",
                )
                if trajectory["format"] != "aipackaging.grid_trajectory" or trajectory["version"] != 1:
                    raise ValueError("unsupported grid_trajectory record")
                trajectory_id = trajectory["trajectoryId"]
                if trajectory_id in trajectory_records:
                    raise ValueError(f"duplicate trajectoryId: {trajectory_id}")
                trajectory_records[trajectory_id] = trajectory
        else:
            raise ValueError(f"unknown shard kind: {shard['kind']}")

    for trajectory_id, trajectory in trajectory_records.items():
        if trajectory["problemId"] not in problem_records:
            raise ValueError(f"unknown trajectory problemId: {trajectory_id}")
        problem = problem_records[trajectory["problemId"]][1]
        problem_json = canonical_json(problem)
        solution = trajectory["finalSolution"]
        if trajectory["solver"] != solution["solver"]:
            raise ValueError(f"solver metadata mismatch: {trajectory_id}")
        if [step["action"] for step in trajectory["steps"]] != solution["placements"]:
            raise ValueError(f"placement replay mismatch: {trajectory_id}")
        error = _native.validate_solution(problem_json, canonical_json(solution))
        if error:
            raise ValueError(f"{trajectory_id}: {error}")
        environment = GridNestingEnv.from_dict(problem)
        environment.reset()
        if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
            raise ValueError(f"action index audit mismatch: {trajectory_id}")
        for step in trajectory["steps"]:
            _require_keys(
                step,
                {"actionIndex", "action", "reward", "rankBefore", "rankAfter", "deltas", "terminated", "truncated"},
                "trajectory step",
            )
            if environment.action(step["actionIndex"]) != step["action"]:
                raise ValueError(f"action audit mismatch: {trajectory_id}")
            _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
            if (
                abs(reward - step["reward"]) > 1e-15
                or info["rankBefore"] != step["rankBefore"]
                or info["rankAfter"] != step["rankAfter"]
                or info["deltas"] != step["deltas"]
            ):
                raise ValueError(f"reward replay mismatch: {trajectory_id}")
            if terminated != step["terminated"] or truncated != step["truncated"]:
                raise ValueError(f"termination replay mismatch: {trajectory_id}")

    for problem_id, expert_id in manifest["expertTrajectoryId"].items():
        if problem_id not in problem_records or expert_id not in trajectory_records:
            raise ValueError(f"broken expertTrajectoryId: {problem_id}")
        candidates = [item for item in trajectory_records.values() if item["problemId"] == problem_id]
        if len(candidates) != len(SOLVERS) or {item["solver"]["name"] for item in candidates} != set(SOLVERS):
            raise ValueError(f"baseline trajectory set mismatch: {problem_id}")
        actual = _best_trajectory(candidates)["trajectoryId"]
        if actual != expert_id:
            raise ValueError(f"wrong expertTrajectoryId: {problem_id}")

    if set(manifest["expertTrajectoryId"]) != set(problem_records):
        raise ValueError("expertTrajectoryId keys do not match problem set")
    return {"problems": len(problem_records), "trajectories": len(trajectory_records), "families": len(family_splits)}
