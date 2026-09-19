#!/usr/bin/env python3
"""Проверяет направления внутренних зависимостей C++ и Python."""

from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


CPP_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


@dataclass(frozen=True)
class Violation:
    """Описывает одно нарушение архитектурного правила."""

    path: Path
    line: int
    message: str


def load_rules(path: Path) -> dict:
    """Загружает машинно-читаемые правила из JSON-файла."""

    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def normalized(path: Path, root: Path) -> str:
    """Возвращает путь относительно репозитория с прямыми разделителями."""

    return path.relative_to(root).as_posix()


def is_ignored(path: Path, root: Path, ignored: set[str]) -> bool:
    """Сообщает, находится ли путь в исключённом каталоге."""

    relative = path.relative_to(root)
    return any(part in ignored for part in relative.parts)


def layer_for(path: Path, layer_roots: dict[str, Path], header_owners: dict[Path, str]) -> str | None:
    """Определяет архитектурного владельца исходного файла."""

    if path in header_owners:
        return header_owners[path]
    for layer, layer_root in layer_roots.items():
        try:
            path.relative_to(layer_root)
            return layer
        except ValueError:
            continue
    return None


def collect_headers(root: Path, layer_roots: dict[str, Path], header_owners: dict[Path, str], ignored: set[str]):
    """Строит индекс внутренних заголовков и выявляет неоднозначные базовые имена."""

    by_basename: dict[str, list[tuple[Path, str]]] = {}
    by_relative: dict[str, tuple[Path, str]] = {}
    for header, layer in header_owners.items():
        if header.exists():
            relative = normalized(header, root)
            by_relative[relative] = (header, layer)
            by_relative[relative.removeprefix("src/solver/include/")] = (header, layer)
            by_basename.setdefault(header.name, []).append((header, layer))
    for layer, layer_root in layer_roots.items():
        if not layer_root.exists():
            continue
        for header in layer_root.rglob("*"):
            if not header.is_file() or header.suffix.lower() not in {".h", ".hpp"}:
                continue
            if is_ignored(header, root, ignored):
                continue
            if header in header_owners:
                continue
            relative = header.relative_to(layer_root).as_posix()
            by_relative.setdefault(relative, (header, layer))
            by_basename.setdefault(header.name, []).append((header, layer))
    return by_basename, by_relative


def resolve_internal_include(
    include: str,
    by_basename: dict[str, list[tuple[Path, str]]],
    by_relative: dict[str, tuple[Path, str]],
) -> tuple[Path, str] | None:
    """Сопоставляет директиву включения с одним внутренним заголовком, если это возможно."""

    normalized_include = include.replace("\\", "/")
    if normalized_include in by_relative:
        return by_relative[normalized_include]
    matches = by_basename.get(Path(normalized_include).name, [])
    if len(matches) == 1:
        return matches[0]
    return None


def scan_cpp(root: Path, rules: dict) -> list[Violation]:
    """Проверяет граф включений C++ по слоям и внешним библиотекам."""

    ignored = set(rules["ignoredDirectories"])
    layer_roots = {name: root / value for name, value in rules["layers"].items()}
    header_owners = {root / path: layer for path, layer in rules.get("headerOwners", {}).items()}
    allowed = {name: set(values) for name, values in rules["allowedInternalDependencies"].items()}
    exceptions = {
        (item["source"], item["include"], item["target"]): item
        for item in rules["includeExceptions"]
    }
    used_exceptions: set[tuple[str, str, str]] = set()
    external = rules["externalIncludes"]
    nlohmann_allowed = set(external["nlohmannAllowedFiles"])
    clipper_allowed = set(external["clipperAllowedFiles"])
    qt_allowed = set(external["qtAllowedLayers"])
    by_basename, by_relative = collect_headers(root, layer_roots, header_owners, ignored)
    violations: list[Violation] = []

    for basename, matches in sorted(by_basename.items()):
        if len(matches) > 1:
            locations = ", ".join(normalized(path, root) for path, _ in matches)
            violations.append(Violation(matches[0][0], 1, f"неоднозначный basename '{basename}': {locations}"))

    sources: set[Path] = set(header_owners)
    for source_root in layer_roots.values():
        if not source_root.exists():
            continue
        for source in source_root.rglob("*"):
            if source.is_file() and source.suffix.lower() in CPP_SUFFIXES and not is_ignored(source, root, ignored):
                sources.add(source)

    for source in sorted(sources):
        source_layer = layer_for(source, layer_roots, header_owners)
        source_name = normalized(source, root)
        for line_number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
                match = INCLUDE_PATTERN.match(line)
                if not match:
                    continue
                include = match.group(1)
                target = resolve_internal_include(include, by_basename, by_relative)
                if target is not None:
                    _, target_layer = target
                    if target_layer not in allowed.get(source_layer, set()):
                        key = (source_name, include, target_layer)
                        if key not in exceptions:
                            violations.append(
                                Violation(source, line_number, f"{source_layer} не может включать {target_layer}: {include}")
                            )
                        else:
                            used_exceptions.add(key)

                if include.startswith(tuple(external["qtPrefixes"])) and source_layer not in qt_allowed:
                    violations.append(Violation(source, line_number, f"Qt include запрещён в слое {source_layer}: {include}"))
                if include.startswith("nlohmann/") and source_name not in nlohmann_allowed:
                    violations.append(Violation(source, line_number, f"nlohmann/json разрешён только в IO implementation: {include}"))
                if include.startswith("clipper2/") and source_name not in clipper_allowed:
                    violations.append(Violation(source, line_number, f"Clipper2 разрешён только в polygon implementation: {include}"))

    for key, item in exceptions.items():
        source = root / item["source"]
        if not item.get("reason") or not item.get("removeBy"):
            violations.append(Violation(source, 1, f"исключение {key[1]} не содержит reason/removeBy"))
        elif key not in used_exceptions:
            violations.append(Violation(source, 1, f"устаревшее исключение зависимости: {key[1]} -> {key[2]}"))
    return violations


def scan_python(root: Path, rules: dict) -> list[Violation]:
    """Проверяет внутренние импорты Python и границу необязательного PyTorch."""

    ignored = set(rules["ignoredDirectories"])
    python_rules = rules["python"]
    torch_allowed = set(python_rules["torchAllowedFiles"])
    module_groups = {
        module: group for group, modules in python_rules.get("moduleGroups", {}).items() for module in modules
    }
    allowed_internal = {
        group: set(targets)
        for group, targets in python_rules.get("allowedInternalDependencies", {}).items()
    }
    violations: list[Violation] = []
    for relative_root in python_rules["roots"]:
        python_root = root / relative_root
        if not python_root.exists():
            continue
        for source in python_root.rglob("*.py"):
            if is_ignored(source, root, ignored):
                continue
            source_name = normalized(source, root)
            try:
                tree = ast.parse(source.read_text(encoding="utf-8"), filename=source_name)
            except SyntaxError as error:
                violations.append(Violation(source, error.lineno or 1, f"не удалось разобрать Python: {error.msg}"))
                continue
            relative_module = source.relative_to(python_root).with_suffix("")
            source_parts = list(relative_module.parts)
            if source_parts[-1] == "__init__":
                source_parts.pop()
            source_module = ".".join(source_parts) or "__init__"
            source_group = _python_group(source_module, module_groups)
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    modules = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom):
                    modules = [node.module or ""]
                else:
                    continue
                if any(name == "torch" or name.startswith("torch.") for name in modules) and source_name not in torch_allowed:
                    violations.append(Violation(source, node.lineno, "PyTorch import запрещён вне training/model/evaluation модулей"))

                imported = _resolve_python_imports(source, python_root, node)
                for module in imported:
                    target_group = _python_group(module, module_groups)
                    if target_group and target_group not in allowed_internal.get(source_group, set()):
                        violations.append(
                            Violation(
                                source,
                                node.lineno,
                                f"Python-модуль {source_group} не может импортировать {target_group}: {module}",
                            )
                        )
    return violations


def _python_group(module: str, module_groups: dict[str, str]) -> str | None:
    """Определяет владельца модуля по самому длинному совпавшему префиксу правила."""

    matches = [prefix for prefix in module_groups if module == prefix or module.startswith(prefix + ".")]
    if not matches:
        return None
    return module_groups[max(matches, key=len)]


def _resolve_python_imports(source: Path, python_root: Path, node: ast.AST) -> list[str]:
    """Возвращает полные имена внутренних модулей для относительных импортов и импорта из пакета."""

    if isinstance(node, ast.Import):
        prefix = python_root.name + "."
        return [alias.name[len(prefix) :] for alias in node.names if alias.name.startswith(prefix)]
    if not isinstance(node, ast.ImportFrom):
        return []
    if node.level == 0:
        if node.module == python_root.name:
            base: list[str] = []
        elif node.module and node.module.startswith(python_root.name + "."):
            base = node.module.split(".")[1:]
        else:
            return []
    else:
        relative = source.relative_to(python_root).with_suffix("")
        package_parts = list(relative.parts)
        package_parts.pop()
        remove_count = max(0, node.level - 1)
        if remove_count > len(package_parts):
            return []
        base = package_parts[: len(package_parts) - remove_count]
        if node.module:
            base.extend(node.module.split("."))

            # Конструкция `from package import module` должна указывать на дочерний модуль, а
            # импорт функции из обычного модуля — на сам модуль.
    result = []
    for alias in node.names:
        child = [*base, alias.name]
        child_path = python_root.joinpath(*child)
        if child_path.with_suffix(".py").exists() or child_path.is_dir():
            result.append(".".join(child))
        else:
            result.append(".".join(base))
    return result


def check_repository(root: Path, rules_path: Path) -> list[Violation]:
    """Возвращает полный список нарушений архитектурных границ."""

    rules = load_rules(rules_path)
    violations = scan_cpp(root, rules) + scan_python(root, rules)
    for relative in rules.get("retiredDirectories", []):
        directory = root / relative
        if directory.exists():
            violations.extend(
                Violation(path, 1, "унаследованный настольный контур запрещён после A8")
                for path in directory.rglob("*")
                if path.is_file()
            )
    for relative in rules.get("retiredFiles", []):
        path = root / relative
        if path.is_file():
            violations.append(Violation(path, 1, "унаследованный настольный файл запрещён после A8"))
    return sorted(violations, key=lambda item: (str(item.path), item.line))


def main() -> int:
    """Разбирает аргументы, запускает проверку и печатает диагностируемый результат."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--rules", type=Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    rules_path = arguments.rules.resolve() if arguments.rules else Path(__file__).with_name("architecture-rules.json")
    violations = check_repository(root, rules_path)
    for violation in violations:
        print(f"{normalized(violation.path, root)}:{violation.line}: архитектура: {violation.message}")
    if violations:
        print(f"Проверка архитектуры не пройдена: нарушений — {len(violations)}.")
        return 1
    print("Проверка архитектуры пройдена.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
