"""Тесты воспроизводимости генератора, manifest и replay датасета."""

from __future__ import annotations

import hashlib
from pathlib import Path

import pytest

from aipackaging_ml.dataset import generate_dataset, verify_dataset
from aipackaging_ml.datasets.serialization import (
    read_canonical_json,
    read_jsonl_gzip,
    sha256_file,
    write_canonical_json,
    write_jsonl_gzip,
)
from aipackaging_ml.generator import PROFILES, family_hash, generate_unique_problem


def _tree_hashes(root: Path) -> dict[str, str]:
    """Возвращает SHA-256 всех файлов дерева по относительным путям."""

    return {
        path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in root.rglob("*")
        if path.is_file()
    }


@pytest.mark.parametrize("tier", ["small", "medium"])
def test_generator_respects_profile_and_is_deterministic(tier: str) -> None:
    """Адрес задачи полностью задаёт байты и все численные диапазоны профиля."""

    owners: dict[str, str] = {}
    first, first_seed = generate_unique_problem(42, tier, "train", 0, owners)
    second, second_seed = generate_unique_problem(42, tier, "train", 0, {})
    assert first == second
    assert first_seed == second_seed
    profile = PROFILES[tier]
    area = first["sheet"]["columns"] * first["sheet"]["rows"]
    part_cells = sum(len(part["cells"]) * part["quantity"] for part in first["parts"])
    instances = sum(part["quantity"] for part in first["parts"])
    assert profile.sheet_min <= first["sheet"]["columns"] <= profile.sheet_max
    assert profile.sheet_min <= first["sheet"]["rows"] <= profile.sheet_max
    assert profile.type_min <= len(first["parts"]) <= profile.type_max
    assert profile.instance_min <= instances <= profile.instance_max
    assert profile.utilization_min <= part_cells / area <= profile.utilization_max
    assert len(family_hash(first)) == 64


def test_single_and_multi_worker_outputs_are_byte_identical(tmp_path: Path) -> None:
    """Число worker не влияет ни на порядок, ни на gzip и manifest."""

    splits = {"train": 1, "validation": 0, "test": 0}
    single = tmp_path / "single"
    parallel = tmp_path / "parallel"
    generate_dataset(single, tiers=("small",), split_sizes=splits, workers=1)
    generate_dataset(parallel, tiers=("small",), split_sizes=splits, workers=2)
    assert _tree_hashes(single) == _tree_hashes(parallel)
    assert verify_dataset(single) == {"problems": 1, "trajectories": 5, "families": 1}


def test_verifier_detects_corrupted_shard(tmp_path: Path) -> None:
    """Повреждение даже одного байта обнаруживается до replay."""

    root = tmp_path / "dataset"
    manifest = generate_dataset(
        root,
        tiers=("small",),
        split_sizes={"train": 1, "validation": 0, "test": 0},
        workers=1,
    )
    shard = root / manifest["shards"][0]["path"]
    shard.write_bytes(shard.read_bytes() + b"corruption")
    with pytest.raises(ValueError, match="checksum mismatch"):
        verify_dataset(root)


def test_verifier_rejects_duplicate_problem_and_damaged_action_audit(tmp_path: Path) -> None:
    """Даже с пересчитанным checksum verifier отклоняет дубли ID и повреждённый replay."""

    root = tmp_path / "dataset"
    generate_dataset(
        root,
        tiers=("small",),
        split_sizes={"train": 1, "validation": 0, "test": 0},
        workers=1,
    )
    manifest_path = root / "manifest.json"
    manifest = read_canonical_json(manifest_path)
    problem_shard = next(
        item for item in manifest["shards"] if item["kind"] == "problems" and item["records"]
    )
    problem_path = root / problem_shard["path"]
    problems = read_jsonl_gzip(problem_path)
    write_jsonl_gzip(problem_path, [*problems, problems[0]])
    problem_shard["records"] += 1
    problem_shard["sha256"] = sha256_file(problem_path)
    write_canonical_json(manifest_path, manifest)
    with pytest.raises(ValueError, match="duplicate problemId"):
        verify_dataset(root)

    write_jsonl_gzip(problem_path, problems)
    problem_shard["records"] -= 1
    problem_shard["sha256"] = sha256_file(problem_path)
    trajectory_shard = next(
        item for item in manifest["shards"] if item["kind"] == "trajectories" and item["records"]
    )
    trajectory_path = root / trajectory_shard["path"]
    trajectories = read_jsonl_gzip(trajectory_path)
    trajectories[0]["actionIndices"] = [-1, *trajectories[0]["actionIndices"][1:]]
    write_jsonl_gzip(trajectory_path, trajectories)
    trajectory_shard["sha256"] = sha256_file(trajectory_path)
    write_canonical_json(manifest_path, manifest)
    with pytest.raises(ValueError, match="action index audit mismatch"):
        verify_dataset(root)
