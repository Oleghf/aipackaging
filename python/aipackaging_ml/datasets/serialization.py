"""Общие примитивы канонической сериализации и проверки файлов датасета."""

from __future__ import annotations

import gzip
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping


def canonical_json(value: Mapping[str, Any]) -> str:
    """Кодирует объект стабильным UTF-8-совместимым JSON без лишних пробелов."""

    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def canonical_line(value: Mapping[str, Any]) -> bytes:
    """Кодирует объект одной канонической UTF-8 строкой JSONL."""

    return (canonical_json(value) + "\n").encode("utf-8")


def write_canonical_json(path: str | Path, value: Mapping[str, Any]) -> None:
    """Записывает канонический JSON с единственным завершающим переводом строки."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(canonical_line(value))


def read_canonical_json(path: str | Path) -> dict[str, Any]:
    """Читает канонический JSON-объект и отклоняет иное байтовое представление."""

    source = Path(path)
    raw = source.read_bytes()
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{source}: JSON root must be an object")
    if raw != canonical_line(value):
        raise ValueError(f"{source}: JSON document is not canonical")
    return value


def write_jsonl_gzip(path: str | Path, records: Iterable[Mapping[str, Any]]) -> None:
    """Записывает canonical JSONL в gzip без timestamp и исходного имени файла."""

    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            for record in records:
                compressed.write(canonical_line(record))


def read_jsonl_gzip(path: str | Path) -> list[dict[str, Any]]:
    """Читает canonical JSONL.gzip и отклоняет повреждённые или неканоничные записи."""

    source = Path(path)
    compressed_bytes = source.read_bytes()
    if len(compressed_bytes) < 10 or compressed_bytes[4:8] != b"\x00\x00\x00\x00":
        raise ValueError(f"{source}: gzip mtime must be zero")

    result: list[dict[str, Any]] = []
    with gzip.open(source, "rt", encoding="utf-8", newline="") as stream:
        for line_number, line in enumerate(stream, 1):
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"{source}:{line_number}: JSONL record must be an object")
            if line.encode("utf-8") != canonical_line(value):
                raise ValueError(f"{source}:{line_number}: JSONL record is not canonical")
            result.append(value)
    return result


def sha256_file(path: str | Path) -> str:
    """Возвращает SHA-256 точного содержимого файла в нижнем регистре."""

    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_keys(value: Any, expected: set[str], label: str) -> Mapping[str, Any]:
    """Отклоняет не-объект, отсутствующие и неизвестные поля строгого контракта."""

    if not isinstance(value, Mapping) or set(value) != expected:
        actual = sorted(value) if isinstance(value, Mapping) else type(value).__name__
        raise ValueError(f"{label} fields mismatch: expected {sorted(expected)}, got {actual}")
    return value


def resolve_dataset_path(root: str | Path, relative_path: str) -> Path:
    """Разрешает относительный путь shard только внутри корневого каталога датасета."""

    root_path = Path(root).resolve()
    candidate = (root_path / relative_path).resolve()
    if candidate == root_path or root_path not in candidate.parents:
        raise ValueError(f"shard path escapes dataset root: {relative_path}")
    return candidate
