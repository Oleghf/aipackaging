#!/usr/bin/env python3
"""Запускает стандартные локальные проверки AIPackaging без установки зависимостей."""

from __future__ import annotations

import argparse
import os
import platform
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(command: list[str], *, expect_failure: str | None = None) -> int:
    """Выполняет команду и возвращает её код, учитывая отрицательный самотест."""

    print("+", subprocess.list2cmdline(command), flush=True)
    completed = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        capture_output=expect_failure is not None,
        text=expect_failure is not None,
    )
    if expect_failure:
        print(completed.stdout, end="")
        print(completed.stderr, end="", file=sys.stderr)
        if completed.returncode == 0:
            print("Ожидалась ошибка команды, но команда завершилась успешно.", file=sys.stderr)
            return 1
        if expect_failure not in completed.stdout + completed.stderr:
            print(f"Не найден ожидаемый признак ошибки: {expect_failure}", file=sys.stderr)
            return 1
        return 0
    return completed.returncode


def run_sequence(commands: list[list[str]]) -> int:
    """Выполняет команды последовательно и останавливается на первой ошибке."""

    for command in commands:
        result = run(command)
        if result:
            return result
    return 0


def semantic_cli_command(build_dir: str) -> list[str]:
    """Формирует команду сравнения устойчивых полей CLI с эталоном A1."""

    return [
        sys.executable,
        "tools/regression/check_cli_semantics.py",
        "--build-dir",
        build_dir,
    ]


def architecture_checks() -> int:
    """Проверяет граф включений и импортов, а также отрицательные тесты правил CMake."""

    result = run_sequence(
        [
            [sys.executable, "tools/architecture/check_dependencies.py"],
            [sys.executable, "-m", "unittest", "discover", "-s", "tools/architecture/tests", "-v"],
        ]
    )
    if result:
        return result
    with tempfile.TemporaryDirectory(prefix="aipackaging-architecture-") as directory:
        return run(
            [
                "cmake",
                "-S",
                str(ROOT / "tools/architecture/cmake-fixture"),
                "-B",
                directory,
            ],
            expect_failure="forbidden public target edge",
        )


def documentation_checks() -> int:
    """Проверяет ссылки и согласованность ключевых фактов документации."""

    return run_sequence(
        [
            [sys.executable, "tools/documentation/check_documentation.py"],
            [sys.executable, "-m", "unittest", "discover", "-s", "tools/documentation/tests", "-v"],
        ]
    )


def headless_checks() -> int:
    """Проверяет предустановку сборки без графического интерфейса для текущей ОС."""

    system = platform.system()
    if system == "Windows":
        preset = "windows-headless-tests"
    elif system == "Linux":
        preset = "linux-headless-tests"
    else:
        print(f"Для ОС {system} нет предустановки проверки без графического интерфейса.", file=sys.stderr)
        return 1
    return run_sequence(
        [
            ["cmake", "--preset", preset],
            ["cmake", "--build", "--preset", f"build-{preset}"],
            ["ctest", "--test-dir", f"build/{preset}", "-C", "Debug", "--output-on-failure"],
            semantic_cli_command(f"build/{preset}"),
        ]
    )


def nesting_checks() -> int:
    """Собирает только ядро, решатель, CLI и их тесты без унаследованного кода и Qt."""

    system = platform.system()
    if system == "Windows":
        preset = "windows-nesting-tests"
    elif system == "Linux":
        preset = "linux-nesting-tests"
    else:
        print(f"Для ОС {system} нет предустановки проверки ядра раскроя.", file=sys.stderr)
        return 1
    return run_sequence(
        [
            ["cmake", "--preset", preset],
            ["cmake", "--build", "--preset", f"build-{preset}"],
            ["ctest", "--test-dir", f"build/{preset}", "-C", "Debug", "--output-on-failure"],
            semantic_cli_command(f"build/{preset}"),
        ]
    )


def python_checks() -> int:
    """Запускает полный набор тестов pytest в подготовленном окружении Python."""

    return run([sys.executable, "-m", "pytest"])


def desktop_checks(qt_dir: str | None, *, with_onnx: bool = True) -> int:
    """Проверяет настольное приложение Windows с выбранной внутренней реализацией."""

    if platform.system() != "Windows":
        print("Проверка настольного приложения сейчас поддерживается только в Windows.", file=sys.stderr)
        return 1
    preset = "windows-desktop-ci-tests" if with_onnx else "windows-desktop-no-onnx-tests"
    build_preset = (
        "build-windows-desktop-ci-tests" if with_onnx else "build-windows-desktop-no-onnx-tests"
    )
    configure = ["cmake", "--preset", preset]
    if qt_dir:
        configure.append(f"-DQt6_DIR={qt_dir}")
    elif not (os.environ.get("Qt6_DIR") or os.environ.get("CMAKE_PREFIX_PATH")):
        print("Путь Qt не указан; CMake выполнит обычный поиск пакета.", flush=True)
    return run_sequence(
        [
            configure,
            ["cmake", "--build", "--preset", build_preset],
            ["ctest", "--test-dir", f"build/{preset}", "-C", "Debug", "--output-on-failure"],
            semantic_cli_command(f"build/{preset}"),
        ]
    )


def main() -> int:
    """Выбирает контур проверки по подкоманде CLI."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "check",
        choices=[
            "architecture",
            "documentation",
            "language",
            "nesting",
            "headless",
            "python",
            "desktop",
            "desktop-no-onnx",
            "all",
        ],
    )
    parser.add_argument("--qt-dir", help="Каталог с Qt6Config.cmake для проверки настольного приложения")
    arguments = parser.parse_args()

    actions = {
        "architecture": architecture_checks,
        "documentation": documentation_checks,
        "language": lambda: run_sequence(
            [
                [sys.executable, "tools/language/check_russian_prose.py"],
                [sys.executable, "-m", "unittest", "discover", "-s", "tools/language/tests", "-v"],
            ]
        ),
        "nesting": nesting_checks,
        "headless": headless_checks,
        "python": python_checks,
        "desktop": lambda: desktop_checks(arguments.qt_dir),
        "desktop-no-onnx": lambda: desktop_checks(arguments.qt_dir, with_onnx=False),
    }
    if arguments.check != "all":
        return actions[arguments.check]()
    for name in (
        "documentation",
        "language",
        "architecture",
        "nesting",
        "headless",
        "python",
        "desktop-no-onnx",
        "desktop",
    ):
        result = actions[name]()
        if result:
            return result
    return 0


if __name__ == "__main__":
    sys.exit(main())
