"""Общая механика воспроизводимого выполнения и публикации частей наборов."""

from __future__ import annotations

import multiprocessing
from collections.abc import Callable, Iterable, Iterator, Mapping, Sequence
from pathlib import Path
from typing import Any, TypeVar

from .serialization import sha256_file, write_jsonl_gzip

_Task = TypeVar("_Task")
_Result = TypeVar("_Result")


def iter_completed(
    worker: Callable[[_Task], _Result], tasks: Sequence[_Task], workers: int
) -> Iterator[_Result]:
    """Выдаёт результаты задач по мере завершения без изменения их содержимого."""

    if workers == 1:
        yield from map(worker, tasks)
        return
    context = multiprocessing.get_context("spawn")
    with context.Pool(processes=workers) as pool:
        yield from pool.imap_unordered(worker, tasks)


def collect_by_key(
    worker: Callable[[_Task], _Result],
    tasks: Sequence[_Task],
    workers: int,
    key: Callable[[_Result], str],
) -> dict[str, _Result]:
    """Собирает результаты независимо от порядка завершения рабочих процессов."""

    return {key(result): result for result in iter_completed(worker, tasks, workers)}


def write_shard(
    root: Path,
    path: Path,
    records: Iterable[Mapping[str, Any]],
    *,
    split: str,
    kind: str,
    metadata: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Записывает каноническую часть набора и возвращает её описатель с SHA-256."""

    materialized = list(records)
    write_jsonl_gzip(path, materialized)
    descriptor = dict(metadata or {})
    descriptor.update(
        {
            "split": split,
            "kind": kind,
            "path": path.relative_to(root).as_posix(),
            "records": len(materialized),
            "sha256": sha256_file(path),
        }
    )
    return descriptor
