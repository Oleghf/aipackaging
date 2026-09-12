"""Проверки общей сериализации, cache и совместимых фасадов A5."""

from __future__ import annotations

import gzip
import json
import subprocess
import sys
from pathlib import Path

import pytest

from aipackaging_ml import contracts, dataset, environment, generator, polygon_dataset
from aipackaging_ml.datasets import serialization
from aipackaging_ml.datasets.cache import read_cache_record, write_cache_record
from aipackaging_ml.datasets.grid import generation as grid_generation
from aipackaging_ml.datasets.grid import pipeline as grid_pipeline
from aipackaging_ml.datasets.grid import verification as grid_verification
from aipackaging_ml.datasets.polygon import generation as polygon_generation
from aipackaging_ml.datasets.polygon import pipeline as polygon_pipeline
from aipackaging_ml.datasets.polygon import verification as polygon_verification


def test_canonical_json_gzip_and_sha_are_shared_and_deterministic(tmp_path: Path) -> None:
    """Общие codecs сохраняют прежние байты независимо от имени выходного gzip."""

    value = {"я": [2, 1], "a": True}
    expected = '{"a":true,"я":[2,1]}\n'.encode("utf-8")
    document = tmp_path / "value.json"
    serialization.write_canonical_json(document, value)
    assert document.read_bytes() == expected
    assert serialization.read_canonical_json(document) == value
    assert contracts.canonical_json(value) == environment.canonical_json(value)
    assert contracts.sha256_file(document) == serialization.sha256_file(document)

    first = tmp_path / "first.jsonl.gz"
    second = tmp_path / "second.jsonl.gz"
    serialization.write_jsonl_gzip(first, [value])
    serialization.write_jsonl_gzip(second, [value])
    assert first.read_bytes() == second.read_bytes()
    assert first.read_bytes()[4:8] == b"\0\0\0\0"
    assert serialization.read_jsonl_gzip(first) == [value]


def test_serialization_rejects_noncanonical_corrupt_and_escaping_data(tmp_path: Path) -> None:
    """Reader отклоняет иное JSON-представление, неверный mtime и выход из dataset root."""

    document = tmp_path / "noncanonical.json"
    document.write_text('{"b": 1, "a": 2}\n', encoding="utf-8")
    with pytest.raises(ValueError, match="not canonical"):
        serialization.read_canonical_json(document)

    shard = tmp_path / "bad.jsonl.gz"
    with shard.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=1) as stream:
            stream.write(json.dumps([1, 2]).encode("utf-8") + b"\n")
    with pytest.raises(ValueError, match="mtime"):
        serialization.read_jsonl_gzip(shard)
    with pytest.raises(ValueError, match="escapes"):
        serialization.resolve_dataset_path(tmp_path, "../outside.jsonl.gz")
    with pytest.raises(ValueError, match="fields mismatch"):
        serialization.require_keys({"known": 1, "extra": 2}, {"known"}, "fixture")


def test_atomic_cache_requires_matching_identity_and_fingerprint(tmp_path: Path) -> None:
    """Cache публикует только каноническую полную запись с ожидаемой конфигурацией."""

    path = tmp_path / "task.json"
    identity = {"tier": "small", "index": 3}
    write_cache_record(path, identity=identity, fingerprint="abc", payload={"result": 7})
    assert read_cache_record(path, expected_identity=identity, expected_fingerprint="abc") == {"result": 7}
    assert not list(tmp_path.glob("*.tmp"))
    with pytest.raises(ValueError, match="identity or fingerprint"):
        read_cache_record(path, expected_identity=identity, expected_fingerprint="other")
    path.write_bytes(b"{}\n")
    with pytest.raises(ValueError, match="fields mismatch"):
        read_cache_record(path, expected_identity=identity, expected_fingerprint="abc")


def test_legacy_dataset_modules_are_thin_compatible_facades() -> None:
    """Старые import paths указывают на новые реализации без изменения публичных объектов."""

    assert generator.generate_problem is grid_generation.generate_problem
    assert dataset.DEFAULT_SPLITS is grid_pipeline.DEFAULT_SPLITS
    assert dataset.generate_dataset is grid_pipeline.generate_dataset
    assert dataset.verify_dataset is grid_verification.verify_dataset
    assert polygon_dataset.generate_polygon_problem is polygon_generation.generate_polygon_problem
    assert polygon_dataset.generate_polygon_dataset is polygon_pipeline.generate_polygon_dataset
    assert polygon_dataset.verify_polygon_dataset is polygon_verification.verify_polygon_dataset


def test_dataset_cli_smoke_does_not_load_torch(tmp_path: Path) -> None:
    """Generate/verify выполняются в чистом процессе, где любой импорт torch запрещён."""

    output = tmp_path / "cli-grid"
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
assert main(["generate-dataset", "--output", {str(output)!r}, "--tier", "small", "--smoke"]) == 0
assert main(["verify-dataset", {str(output)!r}]) == 0
assert not any(name == "torch" or name.startswith("torch.") for name in sys.modules)
"""
    completed = subprocess.run([sys.executable, "-c", script], check=False, capture_output=True, text=True)
    assert completed.returncode == 0, completed.stdout + completed.stderr
