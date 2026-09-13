"""Атомарное хранение внутренних результатов длительной генерации набора данных."""

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
    """Читает кэш при полном совпадении формата, идентификатора и отпечатка."""

    record = read_canonical_json(path)
    require_keys(record, {"format", "version", "identity", "fingerprint", "payload"}, "кэш набора данных")
    if record["format"] != CACHE_FORMAT or record["version"] != CACHE_VERSION:
        raise ValueError("неподдерживаемый формат или версия кэша набора данных")
    if record["identity"] != dict(expected_identity) or record["fingerprint"] != expected_fingerprint:
        raise ValueError("идентификатор или отпечаток кэша набора данных не совпадает")
    if not isinstance(record["payload"], dict):
        raise ValueError("содержимое кэша набора данных должно быть объектом")
    return record["payload"]


def read_compatible_cache_record(
    path: str | Path,
    *,
    expected_identity: Mapping[str, Any],
    expected_fingerprint: str,
) -> dict[str, Any] | None:
    """Возвращает совместимый кэш, игнорирует устаревший и отклоняет повреждённый."""

    record = read_canonical_json(path)
    require_keys(record, {"format", "version", "identity", "fingerprint", "payload"}, "кэш набора данных")
    if record["format"] != CACHE_FORMAT or record["version"] != CACHE_VERSION:
        raise ValueError("неподдерживаемый формат или версия кэша набора данных")
    if not isinstance(record["identity"], dict) or not isinstance(record["fingerprint"], str):
        raise ValueError("идентификатор или отпечаток кэша набора данных имеет неверный тип")
    if record["identity"] != dict(expected_identity) or record["fingerprint"] != expected_fingerprint:
        return None
    if not isinstance(record["payload"], dict):
        raise ValueError("содержимое кэша набора данных должно быть объектом")
    return record["payload"]
