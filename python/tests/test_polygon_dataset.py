"""Тесты воспроизводимости и полной проверки smoke polygon_dataset v1."""

from __future__ import annotations

from pathlib import Path

from aipackaging_ml.polygon_dataset import generate_polygon_dataset, verify_polygon_dataset


def _files(root: Path) -> dict[str, bytes]:
    """Читает относительные имена и точные байты всех файлов датасета."""

    return {path.relative_to(root).as_posix(): path.read_bytes() for path in sorted(root.rglob("*")) if path.is_file()}


def test_polygon_dataset_is_reproducible_across_workers(tmp_path: Path) -> None:
    """Один и два worker создают побайтово одинаковые shards и manifest."""

    sizes = {"train": 1, "validation": 0, "test": 0}
    single = tmp_path / "single"
    parallel = tmp_path / "parallel"
    generate_polygon_dataset(single, split_sizes=sizes, workers=1)
    generate_polygon_dataset(parallel, split_sizes=sizes, workers=2)
    assert _files(single) == _files(parallel)
    assert verify_polygon_dataset(single) == {"problems": 1, "trajectories": 5}
