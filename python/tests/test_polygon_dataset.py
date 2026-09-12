"""Тесты production polygon_dataset v2, совместимости v1 и benchmark."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from aipackaging_ml import _aipackaging_solver as _native
from aipackaging_ml.datasets.polygon import benchmark as benchmark_module
from aipackaging_ml.datasets.polygon import pipeline as pipeline_module
from aipackaging_ml.datasets.polygon.family import polygon_family_hash, problem_features, validate_problem_profile
from aipackaging_ml.datasets.polygon.generation import generate_family_variant, generate_polygon_problem
from aipackaging_ml.datasets.polygon.pipeline import generate_polygon_dataset
from aipackaging_ml.datasets.polygon.recipes import POLYGON_FEATURES
from aipackaging_ml.datasets.polygon.rollout import POLYGON_SMOKE_BUDGETS
from aipackaging_ml.datasets.serialization import (
    canonical_json,
    read_canonical_json,
    read_jsonl_gzip,
    sha256_file,
    write_canonical_json,
    write_jsonl_gzip,
)
from aipackaging_ml.polygon_benchmark import benchmark_polygon_baselines, verify_polygon_benchmark
from aipackaging_ml.polygon_dataset import load_polygon_dataset_records, verify_polygon_dataset

SCHEMAS = Path(__file__).parents[2] / "schemas"


def _published_files(root: Path) -> dict[str, bytes]:
    """Читает точные байты опубликованных файлов, исключая внутренний resume-cache."""

    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file() and ".work" not in path.parts
    }


def _small_dataset(root: Path, workers: int = 1, *, resume: bool = False) -> dict[str, object]:
    """Создаёт две train и две validation задачи с короткими budgets."""

    return generate_polygon_dataset(
        root,
        split_sizes={"train": 2, "validation": 2, "test": 0},
        tiers=("small",),
        workers=workers,
        budgets=POLYGON_SMOKE_BUDGETS,
        resume=resume,
        mode="custom",
    )


@pytest.mark.parametrize(
    ("overrides", "message"),
    [
        ({"split_sizes": {"train": "2", "validation": 2, "test": 0}}, "split size"),
        ({"workers": True}, "workers"),
        ({"budgets": {**POLYGON_SMOKE_BUDGETS, "beamWidth": "2"}}, "budgets"),
        ({"max_attempts": 1.5}, "max_attempts"),
    ],
)
def test_generation_rejects_non_integer_numeric_arguments(
    tmp_path: Path, overrides: dict[str, object], message: str
) -> None:
    """Публичный генератор сообщает об ошибке контракта для неверных числовых типов."""

    arguments: dict[str, object] = {
        "split_sizes": {"train": 2, "validation": 2, "test": 0},
        "tiers": ("small",),
        "workers": 1,
        "budgets": POLYGON_SMOKE_BUDGETS,
        "max_attempts": 2,
        "mode": "custom",
    }
    arguments.update(overrides)
    with pytest.raises(ValueError, match=message):
        generate_polygon_dataset(tmp_path / "invalid", **arguments)


def test_m4_problem_generator_keeps_semantic_golden() -> None:
    """Публичный генератор одной smoke-задачи сохраняет прежние M4-байты."""

    expected = [
        "63a0df1a83b5e21e69b89fe27ba853a47645742d79307e6eedaf01793459c5d8",
        "57b2a41a14f7b2f7cdef3648d05d12736d0b2d824a036840da4b8c63aacac9aa",
        "11dab26edec1436403cd954938d9684d75bf78c9fa1bdd85d128b2a4db877381",
        "e9f32f055eb484a52487212975a4783f1a41b9475b8c002b382dbccc803f4a37",
        "7c10b8ba2496e79a0157e2fefc263a8a55b16a122d1304a09e26d21a65c4c269",
    ]
    actual = []
    for index in range(5):
        problem, _ = generate_polygon_problem(42, "train", index)
        actual.append(hashlib.sha256(canonical_json(problem).encode("utf-8")).hexdigest())
    assert actual == expected


def test_profiles_family_hash_and_feature_coverage_are_deterministic() -> None:
    """Обе scale-вариации совпадают по family hash и покрывают семь классов фигур."""

    assert isinstance(_native.__revision__, str) and _native.__revision__
    assert set(pipeline_module.POLYGON_FEATURES) == set(POLYGON_FEATURES)
    for tier in ("small", "medium"):
        coverage: set[str] = set()
        for family_index in range(8):
            first, _, _ = generate_family_variant(42, tier, "train", family_index, 0, 0)
            second, _, _ = generate_family_variant(42, tier, "train", family_index, 1, 0)
            validate_problem_profile(first, tier)
            validate_problem_profile(second, tier)
            assert polygon_family_hash(first) == polygon_family_hash(second)
            coverage.update(problem_features(first))
        assert coverage == set(POLYGON_FEATURES)


def test_hidden_layout_binding_accepts_only_complete_valid_placements() -> None:
    """Native helper проверяет полный набор, повороты, экземпляры, границы и коллизии."""

    problem, placements, _ = generate_family_variant(42, "small", "train", 0, 0, 0)
    assert placements is not None
    wire = canonical_json(problem)
    metrics = _native.validate_hidden_polygon_layout(wire, placements)
    assert metrics["placedParts"] == metrics["totalParts"] == len(placements)

    with pytest.raises(ValueError, match="incomplete"):
        _native.validate_hidden_polygon_layout(wire, placements[:-1])
    bad_rotation = [dict(item) for item in placements]
    bad_rotation[0]["rotationDegrees"] = 45
    with pytest.raises(ValueError):
        _native.validate_hidden_polygon_layout(wire, bad_rotation)
    duplicate = [dict(item) for item in placements]
    duplicate[1]["partId"] = duplicate[0]["partId"]
    duplicate[1]["instanceIndex"] = duplicate[0]["instanceIndex"]
    with pytest.raises(ValueError):
        _native.validate_hidden_polygon_layout(wire, duplicate)
    overlap = [dict(item) for item in placements]
    overlap[1]["xMicrometers"] = overlap[0]["xMicrometers"]
    overlap[1]["yMicrometers"] = overlap[0]["yMicrometers"]
    with pytest.raises(ValueError):
        _native.validate_hidden_polygon_layout(wire, overlap)
    margin = [dict(item) for item in placements]
    margin[0]["xMicrometers"] = 0
    with pytest.raises(ValueError):
        _native.validate_hidden_polygon_layout(wire, margin)
    outside = [dict(item) for item in placements]
    outside[0]["xMicrometers"] = -1
    with pytest.raises(ValueError):
        _native.validate_hidden_polygon_layout(wire, outside)


def test_dataset_is_reproducible_across_workers_and_resume(tmp_path: Path) -> None:
    """Один/два worker и resume создают одинаковые опубликованные bytes."""

    single = tmp_path / "single"
    parallel = tmp_path / "parallel"
    _small_dataset(single)
    before = _published_files(single)
    _small_dataset(single, workers=2, resume=True)
    assert _published_files(single) == before
    _small_dataset(parallel, workers=2)
    assert _published_files(parallel) == before
    assert verify_polygon_dataset(single) == {"problems": 4, "trajectories": 20, "version": 2}


def test_resume_replaces_stale_cache_and_rejects_matching_corruption(tmp_path: Path) -> None:
    """Другой fingerprint регенерируется, а повреждённый совпавший cache отклоняется."""

    root = tmp_path / "dataset"
    _small_dataset(root)
    changed = generate_polygon_dataset(
        root,
        master_seed=43,
        split_sizes={"train": 2, "validation": 2, "test": 0},
        tiers=("small",),
        budgets=POLYGON_SMOKE_BUDGETS,
        resume=True,
        mode="custom",
    )
    assert changed["masterSeed"] == 43

    cache_path = next((root / ".work").glob("*.json"))
    cache = read_canonical_json(cache_path)
    cache["payload"]["attempt"] = 999
    write_canonical_json(cache_path, cache)
    with pytest.raises(ValueError, match="invalid resume cache"):
        generate_polygon_dataset(
            root,
            master_seed=43,
            split_sizes={"train": 2, "validation": 2, "test": 0},
            tiers=("small",),
            budgets=POLYGON_SMOKE_BUDGETS,
            resume=True,
            mode="custom",
        )


def test_manifest_v2_schema_metadata_and_replay_are_strict(tmp_path: Path) -> None:
    """Schema и verifier подтверждают profiles, hashes, coverage и dynamic replay."""

    root = tmp_path / "dataset"
    manifest = _small_dataset(root)
    schema = json.loads((SCHEMAS / "polygon-dataset-v2.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema).validate(manifest)
    assert manifest["generator"]["tiers"]["small"]["sheetWidth"] == [100, 220]
    assert sum(manifest["coverage"]["small"].values()) > 0

    manifest["unknown"] = True
    write_canonical_json(root / "manifest.json", manifest)
    with pytest.raises(ValueError, match="fields mismatch"):
        verify_polygon_dataset(root)


def test_corrupted_action_and_expert_are_rejected_with_updated_hash(tmp_path: Path) -> None:
    """Verifier обнаруживает подмену действия и expert даже после обновления checksum."""

    root = tmp_path / "dataset"
    _small_dataset(root)
    manifest_path = root / "manifest.json"
    original_manifest = manifest_path.read_bytes()
    manifest = read_canonical_json(manifest_path)
    shard = next(item for item in manifest["shards"] if item["split"] == "train" and item["kind"] == "trajectories")
    shard_path = root / shard["path"]
    original_shard = shard_path.read_bytes()
    records = read_jsonl_gzip(shard_path)
    records[0]["steps"][0]["action"]["xMicrometers"] += 1
    write_jsonl_gzip(shard_path, records)
    shard["sha256"] = sha256_file(shard_path)
    write_canonical_json(manifest_path, manifest)
    with pytest.raises(ValueError, match="(action replay|solution placement audit) mismatch"):
        verify_polygon_dataset(root)

    shard_path.write_bytes(original_shard)
    manifest_path.write_bytes(original_manifest)
    manifest = read_canonical_json(manifest_path)
    problem_id = next(iter(manifest["expertTrajectoryId"]))
    manifest["expertTrajectoryId"][problem_id] = "missing:trajectory"
    write_canonical_json(manifest_path, manifest)
    with pytest.raises(ValueError, match="wrong expertTrajectoryId"):
        verify_polygon_dataset(root)


def test_polygon_dataset_v1_remains_readable(tmp_path: Path) -> None:
    """Verifier сохраняет поддержку strict smoke manifest v1 без миграции файлов."""

    source = tmp_path / "source"
    target = tmp_path / "v1"
    manifest = _small_dataset(source)
    target.mkdir()
    for shard in manifest["shards"]:
        shutil.copy2(source / shard["path"], target / shard["path"])
    legacy = {
        "format": "aipackaging.polygon_dataset",
        "version": 1,
        "problemContractVersion": 1,
        "trajectoryContractVersion": 1,
        "observationVersion": 1,
        "masterSeed": manifest["masterSeed"],
        "revision": manifest["revision"],
        "generator": {
            "name": "deterministic-polygon-smoke",
            "version": 1,
            "splitSizes": manifest["generator"]["splitSizes"],
        },
        "solverBudgets": manifest["solverBudgets"],
        "shards": manifest["shards"],
        "expertTrajectoryId": manifest["expertTrajectoryId"],
    }
    write_canonical_json(target / "manifest.json", legacy)
    assert verify_polygon_dataset(target) == {"problems": 4, "trajectories": 20}


def test_benchmark_uses_frozen_data_distinguishes_ties_and_is_verifiable(tmp_path: Path) -> None:
    """Benchmark не запускает solver и отличает равное лучшему качество от худшего."""

    root = tmp_path / "dataset"
    output = tmp_path / "benchmark.json"
    _small_dataset(root)
    report = benchmark_polygon_baselines(root, output)
    schema = json.loads((SCHEMAS / "polygon-benchmark-report-v1.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema).validate(report)
    assert report["split"] == "validation"
    assert len(report["tasks"]) == 2
    assert sum(item["expertSelections"] for item in report["summaries"].values()) == 2
    assert verify_polygon_benchmark(root, output) == {"tasks": 2, "solvers": 5}
    problem_id = report["tasks"][0]["problemId"]
    _, _, trajectories = load_polygon_dataset_records(root, "validation")
    expert_id = read_canonical_json(root / "manifest.json")["expertTrajectoryId"][problem_id]
    expert = next(item for item in trajectories if item["trajectoryId"] == expert_id)
    assert benchmark_module._quality_relation(expert, expert) == "equivalent_to_best"

    report["summaries"]["beam"]["solved"] += 1
    write_canonical_json(output, report)
    with pytest.raises(ValueError, match="does not match"):
        verify_polygon_benchmark(root, output)


def test_polygon_dataset_cli_smoke_does_not_import_torch(tmp_path: Path) -> None:
    """CLI smoke generate/verify/benchmark работает без train-extra и test leakage."""

    output = tmp_path / "cli-dataset"
    benchmark = tmp_path / "cli-benchmark.json"
    script = f"""
import importlib.abc
import sys

class RejectTorch(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path, target=None):
        if fullname == "torch" or fullname.startswith("torch."):
            raise AssertionError("dataset-only CLI imported PyTorch")
        return None

sys.meta_path.insert(0, RejectTorch())
from aipackaging_ml.__main__ import main
assert main(["generate-polygon-dataset", "--output", {str(output)!r}, "--smoke", "--workers", "2"]) == 0
assert main(["verify-polygon-dataset", {str(output)!r}]) == 0
assert main(["benchmark-polygon-baselines", "--dataset", {str(output)!r}, "--output", {str(benchmark)!r}]) == 0
assert not any(name == "torch" or name.startswith("torch.") for name in sys.modules)
"""
    completed = subprocess.run([sys.executable, "-c", script], check=False, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr
