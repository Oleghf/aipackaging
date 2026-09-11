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
    """Выполняет команду и возвращает её код, учитывая отрицательный self-test."""

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
            print("Expected command to fail, but it succeeded.", file=sys.stderr)
            return 1
        if expect_failure not in completed.stdout + completed.stderr:
            print(f"Expected failure marker was not found: {expect_failure}", file=sys.stderr)
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


def architecture_checks() -> int:
    """Проверяет include/import-граф и отрицательные тесты правил CMake."""

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


def headless_checks() -> int:
    """Конфигурирует, собирает и тестирует headless preset текущей ОС."""

    system = platform.system()
    if system == "Windows":
        preset = "windows-headless-tests"
    elif system == "Linux":
        preset = "linux-headless-tests"
    else:
        print(f"Headless runner does not define a preset for {system}.", file=sys.stderr)
        return 1
    return run_sequence(
        [
            ["cmake", "--preset", preset],
            ["cmake", "--build", "--preset", f"build-{preset}"],
            ["ctest", "--test-dir", f"build/{preset}", "-C", "Debug", "--output-on-failure"],
        ]
    )


def python_checks() -> int:
    """Запускает полный pytest в уже подготовленном Python-окружении."""

    return run([sys.executable, "-m", "pytest"])


def desktop_checks(qt_dir: str | None) -> int:
    """Конфигурирует и тестирует Windows desktop, не сохраняя локальный Qt-путь."""

    if platform.system() != "Windows":
        print("Desktop checks are currently supported only on Windows.", file=sys.stderr)
        return 1
    configure = ["cmake", "--preset", "windows-tests"]
    if qt_dir:
        configure.append(f"-DQt6_DIR={qt_dir}")
    elif not (os.environ.get("Qt6_DIR") or os.environ.get("CMAKE_PREFIX_PATH")):
        print("Qt path was not provided; CMake will use its normal package search.", flush=True)
    return run_sequence(
        [
            configure,
            ["cmake", "--build", "--preset", "build-windows-tests"],
            ["ctest", "--test-dir", "build/windows-tests", "-C", "Debug", "--output-on-failure"],
        ]
    )


def main() -> int:
    """Выбирает контур проверки по подкоманде CLI."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("check", choices=["architecture", "headless", "python", "desktop", "all"])
    parser.add_argument("--qt-dir", help="Каталог с Qt6Config.cmake для desktop-проверки")
    arguments = parser.parse_args()

    actions = {
        "architecture": architecture_checks,
        "headless": headless_checks,
        "python": python_checks,
        "desktop": lambda: desktop_checks(arguments.qt_dir),
    }
    if arguments.check != "all":
        return actions[arguments.check]()
    for name in ("architecture", "headless", "python", "desktop"):
        result = actions[name]()
        if result:
            return result
    return 0


if __name__ == "__main__":
    sys.exit(main())
