#!/usr/bin/env python3
"""Сравнивает устойчивую семантику CLI-решений с эталоном до A2."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def projected_hash(solution_path: Path, projection: dict[str, list[str]]) -> str:
    """Вычисляет SHA-256 только по детерминированным полям решения."""

    solution = json.loads(solution_path.read_text(encoding="utf-8"))
    projected = {key: solution[key] for key in projection["root"] if key in solution}
    for section in ("metrics", "solver"):
        if section in solution:
            projected[section] = {
                key: solution[section][key]
                for key in projection[section]
                if key in solution[section]
            }
    canonical = json.dumps(
        projected,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(canonical).hexdigest()


def find_solution(build_dir: Path, name: str) -> Path | None:
    """Находит единственный результат CLI в дереве сборки с одной или несколькими конфигурациями."""

    matches = sorted(build_dir.rglob(name))
    return matches[0] if len(matches) == 1 else None


def main() -> int:
    """Проверяет наличие и семантический хеш каждого зафиксированного решения."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=ROOT / "tests/regression/cli-semantic-goldens.json",
    )
    arguments = parser.parse_args()
    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    errors: list[str] = []
    for name, expected in manifest["solutions"].items():
        solution_path = find_solution(arguments.build_dir, name)
        if solution_path is None:
            errors.append(f"{name}: результат отсутствует или найден более одного раза")
            continue
        actual = projected_hash(solution_path, manifest["projection"])
        accepted = expected if isinstance(expected, list) else [expected]
        if actual not in accepted:
            errors.append(f"{name}: ожидался один из {accepted}, получен {actual}")
    if errors:
        print("Семантическая регрессия CLI обнаружена:")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"Семантическая проверка CLI пройдена: решений — {len(manifest['solutions'])}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
