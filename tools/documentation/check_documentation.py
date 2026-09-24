#!/usr/bin/env python3
"""Проверяет ссылки и ключевые факты активной документации AIPackaging."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RULES = Path(__file__).with_name("documentation-rules.json")
LINK_PATTERN = re.compile(r"!?\[[^]]*]\(([^)]+)\)")
HEADING_PATTERN = re.compile(r"^#{1,6}\s+(.+?)\s*$")


def read_text(path: Path) -> str:
    """Читает текстовый файл в UTF-8 и возвращает его содержимое."""

    return path.read_text(encoding="utf-8")


def markdown_files(root: Path) -> list[Path]:
    """Возвращает проверяемые Markdown-файлы без каталогов сборки и артефактов."""

    result = [path for path in (root / "README.md", root / "AGENTS.md") if path.is_file()]
    docs = root / "docs"
    if docs.is_dir():
        result.extend(sorted(docs.rglob("*.md")))
    return result


def prose_lines(text: str) -> list[tuple[int, str]]:
    """Возвращает строки Markdown вне ограждённых блоков кода."""

    lines: list[tuple[int, str]] = []
    in_fence = False
    for number, line in enumerate(text.splitlines(), start=1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            lines.append((number, line))
    return lines


def heading_slug(title: str) -> str:
    """Строит совместимый с GitHub идентификатор заголовка Markdown."""

    title = re.sub(r"`([^`]*)`", r"\1", title.lower())
    title = re.sub(r"[^\w\- ]", "", title, flags=re.UNICODE)
    return re.sub(r"-+", "-", title.strip().replace(" ", "-"))


def heading_anchors(path: Path) -> set[str]:
    """Возвращает идентификаторы всех заголовков с учётом повторяющихся имён."""

    counters: Counter[str] = Counter()
    anchors: set[str] = set()
    for _, line in prose_lines(read_text(path)):
        match = HEADING_PATTERN.match(line)
        if not match:
            continue
        base = heading_slug(match.group(1))
        suffix = counters[base]
        counters[base] += 1
        anchors.add(base if suffix == 0 else f"{base}-{suffix}")
    return anchors


def split_link_target(raw_target: str) -> tuple[str, str]:
    """Разделяет локальный путь ссылки и необязательный идентификатор заголовка."""

    target = raw_target.strip()
    if target.startswith("<") and ">" in target:
        target = target[1 : target.index(">")]
    elif " " in target:
        target = target.split(" ", 1)[0]
    path, separator, anchor = target.partition("#")
    return unquote(path), unquote(anchor) if separator else ""


def normalize_spaces(text: str) -> str:
    """Сворачивает последовательности пробельных символов для устойчивого сравнения."""

    return " ".join(text.split())


def project_versions(root: Path) -> dict[str, str]:
    """Извлекает версии проекта из трёх машинных источников истины."""

    sources = {
        "CMakeLists.txt": (root / "CMakeLists.txt", r"project\([^)]*\bVERSION\s+([0-9.]+)"),
        "pyproject.toml": (root / "pyproject.toml", r"(?m)^version\s*=\s*\"([0-9.]+)\""),
        "python/aipackaging_ml/__init__.py": (
            root / "python/aipackaging_ml/__init__.py",
            r"__version__\s*=\s*\"([0-9.]+)\"",
        ),
    }
    versions: dict[str, str] = {}
    for name, (path, pattern) in sources.items():
        if not path.is_file():
            versions[name] = "<файл отсутствует>"
            continue
        match = re.search(pattern, read_text(path), flags=re.DOTALL)
        versions[name] = match.group(1) if match else "<версия не найдена>"
    return versions


def check_links(root: Path, files: list[Path]) -> list[str]:
    """Проверяет существование локальных целей и идентификаторов заголовков."""

    errors: list[str] = []
    anchor_cache: dict[Path, set[str]] = {}
    for source in files:
        for line_number, line in prose_lines(read_text(source)):
            for match in LINK_PATTERN.finditer(line):
                raw_target = match.group(1)
                if re.match(r"^(?:https?://|mailto:)", raw_target, flags=re.IGNORECASE):
                    continue
                link_path, anchor = split_link_target(raw_target)
                target = source if not link_path else (source.parent / link_path).resolve()
                try:
                    target.relative_to(root.resolve())
                except ValueError:
                    errors.append(f"{source.relative_to(root)}:{line_number}: ссылка выходит за репозиторий: {raw_target}")
                    continue
                if not target.exists():
                    errors.append(f"{source.relative_to(root)}:{line_number}: цель ссылки не существует: {raw_target}")
                    continue
                if anchor:
                    if not target.is_file() or target.suffix.lower() != ".md":
                        errors.append(f"{source.relative_to(root)}:{line_number}: якорь задан не для Markdown: {raw_target}")
                        continue
                    anchors = anchor_cache.setdefault(target, heading_anchors(target))
                    if anchor not in anchors:
                        errors.append(f"{source.relative_to(root)}:{line_number}: заголовок ссылки не существует: {raw_target}")
    return errors


def check_rules(root: Path, rules: dict[str, object], files: list[Path]) -> list[str]:
    """Проверяет индекс, версию, этапы, хеши и удалённые документы."""

    errors: list[str] = []
    docs_index = root / "docs/README.md"
    index_targets: set[Path] = set()
    if docs_index.is_file():
        for _, line in prose_lines(read_text(docs_index)):
            for match in LINK_PATTERN.finditer(line):
                path, _ = split_link_target(match.group(1))
                if path and not re.match(r"^(?:https?://|mailto:)", path, flags=re.IGNORECASE):
                    index_targets.add((docs_index.parent / path).resolve())
    for relative in rules.get("requiredIndexLinks", []):
        expected = (root / str(relative)).resolve()
        if expected not in index_targets:
            errors.append(f"docs/README.md: отсутствует обязательная ссылка на {relative}")

    versions = project_versions(root)
    unique_versions = set(versions.values())
    if len(unique_versions) != 1:
        errors.append("версии проекта не совпадают: " + ", ".join(f"{name}={value}" for name, value in versions.items()))
    else:
        version = next(iter(unique_versions))
        for relative in rules.get("versionDocuments", []):
            path = root / str(relative)
            if not path.is_file() or f"`{version}`" not in read_text(path):
                errors.append(f"{relative}: не указана текущая версия `{version}`")

    for relative, snippets in rules.get("requiredText", {}).items():
        path = root / str(relative)
        text = normalize_spaces(read_text(path)) if path.is_file() else ""
        for snippet in snippets:
            if normalize_spaces(str(snippet)) not in text:
                errors.append(f"{relative}: отсутствует обязательный факт: {snippet}")

    for name, specification in rules.get("hashes", {}).items():
        expected = str(specification["value"]).lower()
        for relative in specification["files"]:
            path = root / str(relative)
            text = read_text(path).lower() if path.is_file() else ""
            if expected not in text:
                errors.append(f"{relative}: отсутствует ожидаемый SHA-256 {name}: {expected}")

    removed = [str(value) for value in rules.get("removedDocuments", [])]
    for source in files:
        text = read_text(source)
        for name in removed:
            if name in text:
                errors.append(f"{source.relative_to(root)}: сохранена ссылка либо текстовое упоминание удалённого документа {name}")
    return errors


def check(root: Path, rules_path: Path) -> list[str]:
    """Выполняет полный набор проверок и возвращает диагностические сообщения."""

    rules = json.loads(read_text(rules_path))
    files = markdown_files(root)
    return check_links(root, files) + check_rules(root, rules, files)


def main() -> int:
    """Разбирает параметры, печатает диагностику и возвращает код результата."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="Корень проверяемого репозитория")
    parser.add_argument("--rules", type=Path, default=DEFAULT_RULES, help="Файл правил документации")
    arguments = parser.parse_args()
    errors = check(arguments.root.resolve(), arguments.rules.resolve())
    if errors:
        print(f"Нарушений документации: {len(errors)}", file=sys.stderr)
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("Документация согласована.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
