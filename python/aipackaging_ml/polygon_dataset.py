"""Воспроизводимый smoke-конвейер polygon_dataset и polygon_trajectory v1."""

from __future__ import annotations

import gzip
import hashlib
import json
import multiprocessing
from pathlib import Path
from typing import Any, Iterable, Mapping

from . import _aipackaging_solver as _native
from .environment import PolygonNestingEnv, canonical_json

POLYGON_SOLVERS = ("input-first-fit", "area-left-bottom", "max-side-left-bottom", "random-left-bottom", "beam")
POLYGON_SPLITS = {"train": 16, "validation": 4, "test": 4}


def _point(x: float, y: float) -> dict[str, float]:
    """Создаёт канонический словарь миллиметровой точки."""

    return {"x": round(x, 3), "y": round(y, 3)}


def _line(x: float, y: float) -> dict[str, Any]:
    """Создаёт JSON-сегмент прямой до заданной точки."""

    return {"type": "line", "end": _point(x, y)}


def _shape(kind: int, scale: float) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Возвращает один из выпуклого, вогнутого, дугового, Bézier или дырчатого контуров."""

    width = 16.0 + scale
    height = 12.0 + scale / 2.0
    holes: list[dict[str, Any]] = []
    if kind == 0:
        outer = {"start": _point(0, 0), "segments": [_line(width, 0), _line(width, height), _line(0, height), _line(0, 0)]}
    elif kind == 1:
        outer = {"start": _point(0, 0), "segments": [_line(width, 0), _line(width, height / 2),
                 _line(width / 2, height / 2), _line(width / 2, height), _line(0, height), _line(0, 0)]}
    elif kind == 2:
        radius = round(width / 2, 3)
        width = 2 * radius
        outer = {"start": _point(0, radius), "segments": [
            {"type": "arc", "end": _point(width, radius), "center": _point(radius, radius), "clockwise": False},
            {"type": "arc", "end": _point(0, radius), "center": _point(radius, radius), "clockwise": False},
        ]}
    elif kind == 3:
        outer = {"start": _point(0, 0), "segments": [_line(width, 0), _line(width, height),
                 {"type": "cubic_bezier", "end": _point(0, height),
                  "control1": _point(width * 0.75, height * 1.45),
                  "control2": _point(width * 0.25, height * 1.45)}, _line(0, 0)]}
    else:
        outer = {"start": _point(0, 0), "segments": [_line(width, 0), _line(width, height), _line(0, height), _line(0, 0)]}
        holes = [{"start": _point(width * 0.3, height * 0.3), "segments": [
            _line(width * 0.7, height * 0.3), _line(width * 0.7, height * 0.7),
            _line(width * 0.3, height * 0.7), _line(width * 0.3, height * 0.3)]}]
    return outer, holes


def generate_polygon_problem(master_seed: int, split: str, index: int) -> tuple[dict[str, Any], int]:
    """Строит одну полностью помещающуюся задачу из независимого derived seed."""

    seed_bytes = f"{master_seed}:polygon:{split}:{index}".encode()
    seed = int.from_bytes(hashlib.sha256(seed_bytes).digest()[:8], "big")
    kind = seed % 5
    scale = 0.25 + ((seed >> 8) % 24) * 0.125
    outer, holes = _shape(kind, scale)
    problem = {
        "format": "aipackaging.polygon_problem", "version": 1,
        "problemId": f"polygon-{split}-{index:03d}-{seed:016x}",
        "sheet": {"width": 100.0, "height": 70.0, "unit": "mm"},
        "manufacturing": {"sheetMargin": 2.0, "partSpacing": 1.0, "kerf": 0.2, "curveTolerance": 0.05},
        "parts": [{"id": f"shape-{kind}", "quantity": 2, "outer": outer, "holes": holes,
                   "allowedRotations": [0, 90, 180, 270]}],
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    return problem, seed


def _canonical_line(value: Mapping[str, Any]) -> bytes:
    """Кодирует одну стабильную строку JSONL."""

    return (canonical_json(value) + "\n").encode("utf-8")


def _write_gzip(path: Path, records: Iterable[Mapping[str, Any]]) -> None:
    """Записывает JSONL.gzip без timestamp и имени исходного файла."""

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as stream:
        for record in records:
            stream.write(_canonical_line(record))


def _read_gzip(path: Path) -> list[dict[str, Any]]:
    """Читает canonical JSONL.gzip и проверяет нулевой mtime."""

    if path.read_bytes()[4:8] != b"\0\0\0\0":
        raise ValueError(f"{path}: gzip mtime must be zero")
    result = []
    with gzip.open(path, "rt", encoding="utf-8", newline="") as stream:
        for line in stream:
            value = json.loads(line)
            if line.encode() != _canonical_line(value):
                raise ValueError(f"{path}: non-canonical JSONL")
            result.append(value)
    return result


def _sha256(path: Path) -> str:
    """Возвращает SHA-256 точных байтов файла."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def _require_keys(value: Mapping[str, Any], expected: set[str], label: str) -> None:
    """Отклоняет отсутствующие и неизвестные поля строгого polygon wire-контракта."""

    if set(value) != expected:
        raise ValueError(f"{label} fields mismatch")


def _rollout(task: tuple[dict[str, Any], int]) -> tuple[str, list[dict[str, Any]], str]:
    """Строит и независимо перепроверяет пять baseline-траекторий задачи."""

    problem, seed = task
    wire = canonical_json(problem)
    trajectories = []
    for solver in POLYGON_SOLVERS:
        solution = json.loads(_native.solve_polygon_problem(wire, solver=solver, seed=seed,
                                                            random_iterations=8, beam_width=4,
                                                            max_expanded_states=100, timeout_ms=0))
        error = _native.validate_polygon_solution(wire, canonical_json(solution))
        if error:
            raise RuntimeError(f"{problem['problemId']}/{solver}: {error}")
        environment = PolygonNestingEnv.from_dict(problem)
        environment.reset(seed=seed)
        indices = []
        steps = []
        for placement in solution["placements"]:
            index = environment.find_action(placement)
            action = environment.action(index)
            _, reward, terminated, truncated, info = environment.step(index)
            indices.append(index)
            steps.append({"actionIndex": index, "action": action, "reward": reward,
                          "terminated": terminated, "truncated": truncated, "info": info})
        trajectories.append({"format": "aipackaging.polygon_trajectory", "version": 1,
                             "trajectoryId": f"{problem['problemId']}:{solver}",
                             "problemId": problem["problemId"], "solver": solution["solver"],
                             "actionIndices": indices, "steps": steps, "finalSolution": solution})
    best = trajectories[0]
    for candidate in trajectories[1:]:
        if _native.is_better_polygon_solution(canonical_json(candidate["finalSolution"]),
                                               canonical_json(best["finalSolution"])):
            best = candidate
    return problem["problemId"], trajectories, best["trajectoryId"]


def generate_polygon_dataset(output: str | Path, *, master_seed: int = 42,
                             split_sizes: Mapping[str, int] = POLYGON_SPLITS, workers: int = 1) -> dict[str, Any]:
    """Генерирует polygon_dataset v1 и возвращает записанный manifest."""

    root = Path(output)
    tasks = {split: [generate_polygon_problem(master_seed, split, index) for index in range(split_sizes[split])]
             for split in ("train", "validation", "test")}
    ordered = [task for split in ("train", "validation", "test") for task in tasks[split]]
    if workers == 1:
        results = [_rollout(task) for task in ordered]
    else:
        with multiprocessing.get_context("spawn").Pool(workers) as pool:
            results = pool.map(_rollout, ordered)
    rollouts = {problem_id: (items, expert) for problem_id, items, expert in results}
    shards = []
    experts = {}
    revision = "unknown"
    for split in ("train", "validation", "test"):
        problems = [problem for problem, _ in tasks[split]]
        trajectories = [item for problem in problems for item in rollouts[problem["problemId"]][0]]
        if trajectories:
            revision = trajectories[0]["solver"]["revision"]
        for problem in problems:
            experts[problem["problemId"]] = rollouts[problem["problemId"]][1]
        for kind, path, records in (("problems", root / f"{split}-problems.jsonl.gz", problems),
                                    ("trajectories", root / f"{split}-trajectories.jsonl.gz", trajectories)):
            _write_gzip(path, records)
            shards.append({"split": split, "kind": kind, "path": path.relative_to(root).as_posix(),
                           "records": len(records), "sha256": _sha256(path)})
    manifest = {"format": "aipackaging.polygon_dataset", "version": 1, "problemContractVersion": 1,
                "trajectoryContractVersion": 1, "observationVersion": 1, "masterSeed": master_seed,
                "revision": revision,
                "generator": {"name": "deterministic-polygon-smoke", "version": 1,
                              "splitSizes": dict(split_sizes)},
                "solverBudgets": {"timeoutMs": 0, "randomIterations": 8, "beamWidth": 4,
                                  "maxExpandedStates": 100},
                "shards": shards, "expertTrajectoryId": dict(sorted(experts.items()))}
    root.mkdir(parents=True, exist_ok=True)
    (root / "manifest.json").write_text(canonical_json(manifest) + "\n", encoding="utf-8", newline="\n")
    return manifest


def verify_polygon_dataset(path: str | Path) -> dict[str, int]:
    """Проверяет checksums, strict решения и динамический replay всех траекторий."""

    root = Path(path)
    manifest_text = (root / "manifest.json").read_text(encoding="utf-8")
    manifest = json.loads(manifest_text)
    _require_keys(manifest, {"format", "version", "problemContractVersion", "trajectoryContractVersion",
                             "observationVersion", "masterSeed", "revision", "generator", "solverBudgets", "shards",
                             "expertTrajectoryId"}, "manifest")
    if manifest.get("format") != "aipackaging.polygon_dataset" or manifest.get("version") != 1:
        raise ValueError("unsupported polygon_dataset manifest")
    if manifest_text.encode("utf-8") != _canonical_line(manifest):
        raise ValueError("polygon manifest is not canonical")
    _require_keys(manifest["generator"], {"name", "version", "splitSizes"}, "generator")
    _require_keys(manifest["generator"]["splitSizes"], {"train", "validation", "test"}, "split sizes")
    _require_keys(manifest["solverBudgets"], {"timeoutMs", "randomIterations", "beamWidth", "maxExpandedStates"},
                  "solver budgets")
    problems: dict[str, dict[str, Any]] = {}
    trajectories: dict[str, dict[str, Any]] = {}
    family_splits: dict[str, str] = {}
    for shard in manifest["shards"]:
        _require_keys(shard, {"split", "kind", "path", "records", "sha256"}, "shard")
        if shard["split"] not in {"train", "validation", "test"} or shard["kind"] not in {"problems", "trajectories"}:
            raise ValueError(f"invalid shard discriminator: {shard['path']}")
        shard_path = (root / shard["path"]).resolve()
        if root.resolve() not in shard_path.parents or _sha256(shard_path) != shard["sha256"]:
            raise ValueError(f"invalid shard: {shard['path']}")
        records = _read_gzip(shard_path)
        if len(records) != shard["records"]:
            raise ValueError(f"record count mismatch: {shard['path']}")
        target = problems if shard["kind"] == "problems" else trajectories
        key = "problemId" if shard["kind"] == "problems" else "trajectoryId"
        for record in records:
            if shard["kind"] == "trajectories":
                _require_keys(record, {"format", "version", "trajectoryId", "problemId", "solver",
                                       "actionIndices", "steps", "finalSolution"}, "trajectory")
                if record["format"] != "aipackaging.polygon_trajectory" or record["version"] != 1:
                    raise ValueError("unsupported polygon_trajectory")
            else:
                signature_source = {"sheet": record["sheet"], "manufacturing": record["manufacturing"],
                                    "parts": record["parts"]}
                signature = hashlib.sha256(canonical_json(signature_source).encode()).hexdigest()
                owner = family_splits.setdefault(signature, shard["split"])
                if owner != shard["split"]:
                    raise ValueError(f"polygon family split leakage: {record['problemId']}")
            if record[key] in target:
                raise ValueError(f"duplicate {key}: {record[key]}")
            target[record[key]] = record
    for trajectory in trajectories.values():
        problem = problems[trajectory["problemId"]]
        wire = canonical_json(problem)
        if _native.validate_polygon_solution(wire, canonical_json(trajectory["finalSolution"])):
            raise ValueError(f"invalid final solution: {trajectory['trajectoryId']}")
        environment = PolygonNestingEnv.from_dict(problem)
        environment.reset()
        if trajectory["actionIndices"] != [step["actionIndex"] for step in trajectory["steps"]]:
            raise ValueError(f"action index audit mismatch: {trajectory['trajectoryId']}")
        for step in trajectory["steps"]:
            _require_keys(step, {"actionIndex", "action", "reward", "terminated", "truncated", "info"},
                          "trajectory step")
            if environment.action(step["actionIndex"]) != step["action"]:
                raise ValueError(f"action replay mismatch: {trajectory['trajectoryId']}")
            _, reward, terminated, truncated, info = environment.step(step["actionIndex"])
            if reward != step["reward"] or terminated != step["terminated"] or truncated != step["truncated"] or info != step["info"]:
                raise ValueError(f"transition replay mismatch: {trajectory['trajectoryId']}")
    if set(manifest["expertTrajectoryId"]) != set(problems):
        raise ValueError("expertTrajectoryId keys mismatch")
    for problem_id, expert_id in manifest["expertTrajectoryId"].items():
        candidates = [value for value in trajectories.values() if value["problemId"] == problem_id]
        if len(candidates) != len(POLYGON_SOLVERS) or {item["solver"]["name"] for item in candidates} != set(POLYGON_SOLVERS):
            raise ValueError(f"baseline trajectory set mismatch: {problem_id}")
        best = candidates[0]
        for candidate in candidates[1:]:
            if _native.is_better_polygon_solution(canonical_json(candidate["finalSolution"]),
                                                   canonical_json(best["finalSolution"])):
                best = candidate
        if best["trajectoryId"] != expert_id:
            raise ValueError(f"wrong expertTrajectoryId: {problem_id}")
    return {"problems": len(problems), "trajectories": len(trajectories)}
