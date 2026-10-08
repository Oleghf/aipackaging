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
        env={**os.environ, **({"VSLANG": "1033"} if platform.system() == "Windows" else {})},
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


def language_checks() -> int:
    """Проверяет русскую прозу и наличие обязательных шапок C++."""

    return run_sequence(
        [
            [sys.executable, "tools/language/check_russian_prose.py"],
            [sys.executable, "tools/language/check_cpp_headers.py"],
            [sys.executable, "-m", "unittest", "discover", "-s", "tools/language/tests", "-v"],
        ]
    )


def format_checks() -> int:
    """Проверяет форматирование всех отслеживаемых собственных файлов C++."""

    return run([sys.executable, "tools/quality/check_cpp_format.py"])


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

    return run_sequence([
        [sys.executable, "tools/quality/check_native_freshness.py"],
        [sys.executable, "-m", "unittest", "discover", "-s", "tools/quality/tests", "-v"],
        [sys.executable, "-m", "pytest"],
    ])


def editor_performance_checks() -> int:
    """Собирает Release-пробу редактора и проверяет измеримые пределы M8.3."""

    system = platform.system()
    if system == "Windows":
        preset = "windows-editor-performance"
        executable = ROOT / "build" / preset / "tests" / "AIPackaging_EditorPerformanceProbe.exe"
    elif system == "Linux":
        preset = "linux-editor-performance"
        executable = ROOT / "build" / preset / "tests" / "AIPackaging_EditorPerformanceProbe"
    else:
        print(f"Для ОС {system} нет измерительной предустановки редактора.", file=sys.stderr)
        return 1
    command = [str(executable)]
    if os.environ.get("CI"):
        command.append("--ci")
    shared_dependencies = ROOT / "build" / (
        "windows-headless-tests" if system == "Windows" else "linux-headless-tests"
    ) / "_deps"
    configure = ["cmake", "--preset", preset]
    dependency_sources = {
        "NLOHMANN_JSON": shared_dependencies / "nlohmann_json-src",
        "CLIPPER2": shared_dependencies / "clipper2-src",
        "GOOGLETEST": ROOT / "build" / "windows-tests" / "_deps" / "googletest-src"
        if system == "Windows"
        else shared_dependencies / "googletest-src",
    }
    for name, path in dependency_sources.items():
        if path.is_dir():
            configure.append(f"-DFETCHCONTENT_SOURCE_DIR_{name}={path}")
    return run_sequence(
        [
            configure,
            ["cmake", "--build", "--preset", f"build-{preset}", "--target", "AIPackaging_EditorPerformanceProbe"],
            [*command, "--segments", "500"],
            [*command, "--segments", "1000"],
        ]
    )


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


def gui_acceptance_checks(qt_dir: str | None) -> int:
    """Выполняет отдельную приёмку в обеих настольных конфигурациях, сохраняя все отказы."""
    if platform.system() != "Windows":
        print("Программная приёмка настроена для Windows.", file=sys.stderr)
        return 1
    failed = False
    for preset in ("windows-desktop-no-onnx-tests", "windows-desktop-ci-tests"):
        configure = ["cmake", "--preset", preset]
        if qt_dir:
            configure.append(f"-DQt6_DIR={qt_dir}")
        result = run_sequence([configure, ["cmake", "--build", "--preset", f"build-{preset}",
                                          "--target", "AIPackaging_GuiAcceptanceTests", "--parallel", "4"]])
        if result:
            failed = True
            continue
        failed = bool(run(["ctest", "--test-dir", f"build/{preset}", "-C", "Debug", "-L", "gui-acceptance",
                           "--output-on-failure"])) or failed
    return int(failed)


def gui_environment_diagnostic() -> int:
    """Повторяет ограничение мастера `offscreen` отдельно от оконной приёмки, сохраняя ненулевой результат."""
    failed = False
    for preset in ("windows-desktop-no-onnx-tests", "windows-desktop-ci-tests"):
        executable = ROOT / "build" / preset / "tests/AIPackaging_GuiAcceptanceTests.exe"
        for scale in ("1", "1.5", "2"):
            environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR=scale)
            result = subprocess.run([str(executable), "--gtest_filter=GuiAcceptance.DxfNavigationButtonIsReachable"],
                                    cwd=ROOT, env=environment, check=False)
            failed = result.returncode != 0 or failed
    return int(failed)


def gui_performance_checks(qt_dir: str | None) -> int:
    """Измеряет оба размера в отдельных процессах Release без изменения настольных кэшей."""
    if platform.system() != "Windows":
        print("Измерение GUI настроено для Windows.", file=sys.stderr)
        return 1
    directory = ROOT / "build/windows-gui-performance"
    configure = ["cmake", "--preset", "windows-desktop-no-onnx-tests", "-B", str(directory),
                 "-DCMAKE_BUILD_TYPE=Release"]
    if qt_dir:
        configure.append(f"-DQt6_DIR={qt_dir}")
    for name in ("googletest", "nlohmann_json", "clipper2"):
        for preset in ("windows-desktop-no-onnx-tests", "windows-headless-tests", "windows-tests"):
            source = ROOT / "build" / preset / "_deps" / f"{name}-src"
            if (source / "CMakeLists.txt").is_file():
                configure.append(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={source}")
                break
    result = run_sequence([configure, ["cmake", "--build", str(directory), "--target",
                                      "AIPackaging_GuiAcceptanceTests", "--parallel", "4"]])
    if result:
        return result
    print(f"Окружение: {platform.platform()}; процессор: {platform.processor()}; Release, Qt: {qt_dir}", flush=True)
    run(["git", "rev-parse", "HEAD"])
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_SCALE_FACTOR="1")
    failed = False
    for segments in (500, 1000):
        command = [str(directory / "tests/AIPackaging_GuiAcceptanceTests.exe"), "--performance", str(segments)]
        print("+", subprocess.list2cmdline(command), flush=True)
        failed = subprocess.run(command, cwd=ROOT, env=environment, timeout=180).returncode != 0 or failed
    return int(failed)


def main() -> int:
    """Выбирает контур проверки по подкоманде CLI."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "check",
        choices=[
            "architecture",
            "documentation",
            "language",
            "format",
            "nesting",
            "headless",
            "python",
            "editor-performance",
            "desktop",
            "desktop-no-onnx",
            "gui-acceptance",
            "gui-environment-diagnostic",
            "gui-performance",
            "all",
        ],
    )
    parser.add_argument("--qt-dir", help="Каталог с Qt6Config.cmake для проверки настольного приложения")
    arguments = parser.parse_args()

    actions = {
        "architecture": architecture_checks,
        "documentation": documentation_checks,
        "language": language_checks,
        "format": format_checks,
        "nesting": nesting_checks,
        "headless": headless_checks,
        "python": python_checks,
        "editor-performance": editor_performance_checks,
        "desktop": lambda: desktop_checks(arguments.qt_dir),
        "desktop-no-onnx": lambda: desktop_checks(arguments.qt_dir, with_onnx=False),
        "gui-acceptance": lambda: gui_acceptance_checks(arguments.qt_dir),
        "gui-environment-diagnostic": gui_environment_diagnostic,
        "gui-performance": lambda: gui_performance_checks(arguments.qt_dir),
    }
    if arguments.check != "all":
        return actions[arguments.check]()
    for name in (
        "documentation",
        "language",
        "format",
        "architecture",
        "nesting",
        "headless",
        "python",
        "editor-performance",
        "desktop-no-onnx",
        "desktop",
    ):
        result = actions[name]()
        if result:
            return result
    return 0


if __name__ == "__main__":
    sys.exit(main())
