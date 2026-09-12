"""Атомарное хранение внутренних результатов долгой генерации датасета."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any, Mapping

from .serialization import canonical_line, read_canonical_json, require_keys

CACHE_FORMAT = "aipackaging.dataset_task_cache"
CACHE_VERSION = 1


def write_cache_record(
    path: str | Path,
    *,
    identity: Mapping[str, Any],
    fingerprint: str,
    payload: Mapping[str, Any],
) -> None:
    """Атомарно записывает полностью сформированный результат одной задачи генератора."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
    record = {
        "format": CACHE_FORMAT,
        "version": CACHE_VERSION,
        "identity": dict(identity),
        "fingerprint": fingerprint,
        "payload": dict(payload),
    }
    try:
        temporary.write_bytes(canonical_line(record))
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


def read_cache_record(
    path: str | Path,
    *,
    expected_identity: Mapping[str, Any],
    expected_fingerprint: str,
) -> dict[str, Any]:
    """Читает cache только при полном совпадении формата, identity и fingerprint."""

    record = read_canonical_json(path)
    require_keys(record, {"format", "version", "identity", "fingerprint", "payload"}, "dataset cache")
    if record["format"] != CACHE_FORMAT or record["version"] != CACHE_VERSION:
        raise ValueError("unsupported dataset cache format or version")
    if record["identity"] != dict(expected_identity) or record["fingerprint"] != expected_fingerprint:
        raise ValueError("dataset cache identity or fingerprint mismatch")
    if not isinstance(record["payload"], dict):
        raise ValueError("dataset cache payload must be an object")
    return record["payload"]
