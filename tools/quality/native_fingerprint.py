"""Вычисляет отпечаток собственных входов нативной привязки без чтения Git HEAD."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE_ROOTS = ("src/nesting", "src/solver", "src/python", "src/support", "cmake")


def fingerprint(root: Path = ROOT) -> str:
    """Хеширует имена и содержимое исходников, включая новые и незакоммиченные файлы."""
    paths = {root / name for name in ("CMakeLists.txt", "src/CMakeLists.txt", "pyproject.toml",
                                     "tools/quality/native_fingerprint.py")}
    for directory in SOURCE_ROOTS:
        paths.update(path for path in (root / directory).rglob("*") if path.is_file() and (
            path.suffix in {".cpp", ".h", ".hpp", ".cc", ".cxx", ".cmake"} or path.name == "CMakeLists.txt"))
    digest = hashlib.sha256()
    for path in sorted(paths, key=lambda item: item.relative_to(root).as_posix()):
        name = path.relative_to(root).as_posix().encode("utf-8")
        data = path.read_bytes().replace(b"\r\n", b"\n")
        digest.update(len(name).to_bytes(8, "big") + name)
        digest.update(len(data).to_bytes(8, "big") + data)
    return digest.hexdigest()


def main() -> int:
    """Печатает отпечаток либо обновляет только изменившийся служебный заголовок сборки."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--header", type=Path, help="служебный заголовок каталога сборки")
    args = parser.parse_args()
    value = fingerprint()
    if args.header:
        text = f'#define AIPACKAGING_SOURCE_FINGERPRINT "{value}"\n'
        if not args.header.exists() or args.header.read_text(encoding="utf-8") != text:
            args.header.parent.mkdir(parents=True, exist_ok=True)
            args.header.write_text(text, encoding="utf-8", newline="\n")
    else:
        print(value)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
