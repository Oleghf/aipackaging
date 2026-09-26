#!/usr/bin/env python3
"""Проверяет форматирование всех отслеживаемых собственных файлов C++."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
OWN_ROOTS = {"src", "tests"}


def find_clang_format() -> str | None:
    """Ищет `clang-format` в настройке, `PATH` и типовых установках LLVM Visual Studio."""

    configured = os.environ.get("CLANG_FORMAT")
    if configured and Path(configured).is_file():
        return configured
    located = shutil.which("clang-format")
    if located:
        return located
    if os.name != "nt":
        return None
    program_files = Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
    pattern = "Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-format.exe"
    candidates = sorted(program_files.glob(pattern), reverse=True)
    return str(candidates[0]) if candidates else None


def tracked_cpp_files(root: Path = ROOT) -> list[Path]:
    """Возвращает отслеживаемые Git собственные исходники без зависимостей и результатов сборки."""

    completed = subprocess.run(
        ["git", "ls-files", "-z"], cwd=root, check=False, capture_output=True
    )
    if completed.returncode != 0:
        raise RuntimeError("Не удалось получить список отслеживаемых файлов Git")
    result: list[Path] = []
    for raw in completed.stdout.split(b"\0"):
        if not raw:
            continue
        relative = Path(os.fsdecode(raw))
        if relative.parts and relative.parts[0] in OWN_ROOTS and relative.suffix.lower() in SUFFIXES:
            result.append(root / relative)
    return sorted(result)


def main() -> int:
    """Запускает сухую проверку частями и возвращает первый код ошибки форматирования."""

    executable = find_clang_format()
    if not executable:
        print("Не найден clang-format: задайте CLANG_FORMAT, PATH или установите LLVM в Visual Studio.", file=sys.stderr)
        return 2
    files = tracked_cpp_files()
    for offset in range(0, len(files), 40):
        command = [executable, "--dry-run", "--Werror", *map(str, files[offset : offset + 40])]
        completed = subprocess.run(command, cwd=ROOT, check=False)
        if completed.returncode:
            return completed.returncode
    print(f"Форматирование C++ проверено: {len(files)} файлов.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
