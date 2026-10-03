"""Не допускает проверки текущего Python-пакета с устаревшей нативной привязкой."""

from __future__ import annotations

import sys
import subprocess
import tomllib
from pathlib import Path
from importlib.machinery import EXTENSION_SUFFIXES

from native_fingerprint import ROOT, fingerprint


def matches_native(native, package_version: str, expected_version: str, expected_digest: str) -> bool:
    """Сопоставляет происхождение модуля и обе версии без изменения загруженного объекта."""
    origin = getattr(native, "__file__", None)
    return bool(origin) and Path(origin).is_file() and any(str(origin).endswith(suffix) for suffix in EXTENSION_SUFFIXES) and (
        getattr(native, "__version__", None) == expected_version == package_version
        and getattr(native, "__source_fingerprint__", None) == expected_digest
    )


def main() -> int:
    """Проверяет версию и отпечаток, не пересобирая и не изменяя окружение."""
    try:
        from aipackaging_ml import _aipackaging_solver as native, __version__
        expected = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))["project"]["version"]
        print(f"Нативный модуль: {native.__file__}")
        if matches_native(native, __version__, expected, fingerprint()):
            print("Версия и отпечаток исходников нативного модуля совпадают.")
            return 0
    except (ImportError, OSError, AttributeError) as error:
        print(f"Не удалось проверить нативный модуль: {error}", file=sys.stderr)
    print("Нативный модуль устарел или не содержит отпечатка. Выполните из корня проекта:", file=sys.stderr)
    command = [sys.executable, "-m", "pip", "install", "--no-deps", "--no-build-isolation", "--no-index", "-e", "."]
    print(subprocess.list2cmdline(command), file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
