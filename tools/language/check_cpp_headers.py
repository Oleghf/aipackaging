#!/usr/bin/env python3
"""Проверяет наличие русских шапок у собственных объявлений и определений C++."""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RULES = Path(__file__).with_name("cpp-header-rules.json")
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
CYRILLIC = re.compile(r"[А-Яа-яЁё]")
CLASS_PATTERN = re.compile(r"\b(class|struct)\s+(?:\w+\s+)*(\w+)\s*(?::[^;{]+)?\{")
FUNCTION_PATTERN = re.compile(
    r"(?P<name>(?:[A-Za-z_]\w*::)*(?:~?[A-Za-z_]\w*|operator\s*(?:\[\]|\(\)|[^\s(]+)))\s*\([^;{}]*\)"
    r"\s*(?:const\b\s*)?(?:noexcept(?:\s*\([^)]*\))?\s*)?(?:override\s*)?(?:final\s*)?(?:->\s*[^;{=]+\s*)?"
    r"(?P<ending>=\s*(?:delete|default)\s*;|[;{])",
    re.DOTALL,
)
CONTROL_NAMES = {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "static_assert"}


@dataclass(frozen=True)
class Finding:
    """Описывает объявление без обязательной русской шапки."""

    path: Path
    line: int
    symbol: str
    kind: str


@dataclass(frozen=True)
class Rules:
    """Содержит область проверки и точечные объяснённые исключения."""

    scan_roots: tuple[str, ...]
    ignored_directories: frozenset[str]
    excluded_files: frozenset[str]
    exceptions: frozenset[tuple[str, str]]


def load_rules(path: Path = DEFAULT_RULES) -> Rules:
    """Загружает конфигурацию и отклоняет исключения без `path`, `symbol` или `reason`."""

    document = json.loads(path.read_text(encoding="utf-8"))
    exceptions: set[tuple[str, str]] = set()
    for item in document.get("exceptions", []):
        if set(item) != {"path", "symbol", "reason"} or not all(item.values()):
            raise ValueError("Исключение шапки должно содержать непустые путь, символ и причину")
        exceptions.add((item["path"].replace("\\", "/"), item["symbol"]))
    return Rules(
        scan_roots=tuple(document["scanRoots"]),
        ignored_directories=frozenset(document["ignoredDirectories"]),
        excluded_files=frozenset(path.replace("\\", "/") for path in document.get("excludedFiles", [])),
        exceptions=frozenset(exceptions),
    )


def _mask_comments_and_literals(text: str) -> str:
    """Заменяет комментарии и литералы пробелами, сохраняя строки и документирующие комментарии отдельно."""

    result = list(text)
    index = 0
    state = "code"
    while index < len(text):
        pair = text[index : index + 2]
        if state == "code" and pair == "//":
            end = text.find("\n", index)
            end = len(text) if end < 0 else end
            for position in range(index, end):
                result[position] = " "
            index = end
            continue
        if state == "code" and pair == "/*":
            end = text.find("*/", index + 2)
            end = len(text) - 2 if end < 0 else end
            for position in range(index, min(len(text), end + 2)):
                if result[position] != "\n":
                    result[position] = " "
            index = end + 2
            continue
        if state == "code" and text[index] in {'"', "'"}:
            state = text[index]
            result[index] = " "
            index += 1
            continue
        if state in {'"', "'"}:
            if text[index] == "\\":
                result[index] = " "
                if index + 1 < len(text):
                    result[index + 1] = " " if text[index + 1] != "\n" else "\n"
                index += 2
                continue
            if text[index] == state:
                state = "code"
            if text[index] != "\n":
                result[index] = " "
        index += 1
    return "".join(result)


def _line_number(text: str, offset: int) -> int:
    """Преобразует смещение символа в номер строки, начинающийся с единицы."""

    return text.count("\n", 0, offset) + 1


def _has_russian_header(lines: list[str], line: int) -> bool:
    """Ищет непосредственно перед символом непрерывную шапку `///`, пропуская атрибуты и шаблон."""

    index = line - 2
    while index >= 0:
        stripped = lines[index].strip()
        if not stripped:
            index -= 1
            continue
        if stripped.startswith(("template", "[[", "Q_")) or stripped in {"public:", "protected:", "private:", "signals:", "slots:"}:
            index -= 1
            continue
        break
    comments: list[str] = []
    while index >= 0 and lines[index].lstrip().startswith("///"):
        comments.append(lines[index])
        index -= 1
    return bool(comments) and any(CYRILLIC.search(comment) for comment in comments)


def _looks_like_macro(text: str, name: str) -> bool:
    """Отбрасывает определения макросов и вызовы макросов с полностью прописным именем."""

    prefix = text.lstrip()
    base = name.rsplit("::", 1)[-1]
    return prefix.startswith("#") or (base.isupper() and "::" not in name)


def _looks_like_function(masked: str, match: re.Match[str], is_header: bool) -> bool:
    """Отличает сигнатуру от вызова по контексту строки и квалификатору имени."""

    name = re.sub(r"\s+", "", match.group("name"))
    base = name.rsplit("::", 1)[-1]
    if "[" in masked[match.start() : match.end()].replace("operator[]", ""):
        return False
    line_start = masked.rfind("\n", 0, match.start()) + 1
    prefix = masked[line_start : match.start()].strip()
    if base in CONTROL_NAMES or _looks_like_macro(masked[line_start : match.end()], name):
        return False
    if prefix.startswith("#") or any(token in prefix for token in ("=", ".", "->", "return ", "co_return ", "new ", "[")):
        return False
    if name.startswith(("std::", "QObject::", "QTimer::", "py::")):
        return False
    if "std::function<" in prefix or "decltype(" in prefix:
        return False
    ending = match.group("ending")
    if not is_header and ending == ";":
        return False
    if prefix:
        return bool(re.search(r"(?:^|\s)[~A-Za-z_]\w*(?:\s*[*&]+)?\s*$", prefix))
    if "::" not in name:
        return is_header
    qualifier = name.rsplit("::", 2)[-2]
    return base.lstrip("~") == qualifier


def scan_file(path: Path, root: Path, rules: Rules) -> list[Finding]:
    """Находит однозначные классы и функции без русской шапки в одном собственном файле."""

    relative = path.relative_to(root).as_posix()
    if relative in rules.excluded_files:
        return []
    text = path.read_text(encoding="utf-8")
    masked = _mask_comments_and_literals(text)
    lines = text.splitlines()
    findings: list[Finding] = []
    seen: set[tuple[int, str]] = set()

    for match in CLASS_PATTERN.finditer(masked):
        line = _line_number(masked, match.start())
        symbol = match.group(2)
        key = (line, symbol)
        if key not in seen and (relative, symbol) not in rules.exceptions and not _has_russian_header(lines, line):
            findings.append(Finding(path, line, symbol, "класс или структура"))
            seen.add(key)

    for match in FUNCTION_PATTERN.finditer(masked):
        name = re.sub(r"\s+", "", match.group("name"))
        if not _looks_like_function(masked, match, path.suffix.lower() in {".h", ".hpp"}):
            continue
        line = _line_number(masked, match.start())
        statement_line = lines[line - 1].lstrip() if line <= len(lines) else ""
        if statement_line.startswith(("using ", "typedef ", "return ")) or "[]" in masked[max(0, match.start() - 3) : match.start()]:
            continue
        ending = match.group("ending")
        is_header = path.suffix.lower() in {".h", ".hpp"}
        if is_header and ending == "{" and "::" not in name:
            # Короткие встроенные методы также являются объявлениями публичного контракта.
            pass
        elif not is_header and ending == ";" and not ending.startswith("="):
            continue
        key = (line, name)
        if key in seen or (relative, name) in rules.exceptions or _has_russian_header(lines, line):
            continue
        kind = "удалённая или стандартная функция" if ending.startswith("=") else ("объявление функции" if is_header else "определение функции")
        findings.append(Finding(path, line, name, kind))
        seen.add(key)
    return findings


def collect_findings(root: Path = ROOT, rules: Rules | None = None) -> list[Finding]:
    """Проверяет настроенные каталоги, исключая внешние и сгенерированные деревья."""

    active_rules = rules or load_rules()
    findings: list[Finding] = []
    for scan_root in active_rules.scan_roots:
        directory = root / scan_root
        if not directory.exists():
            continue
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
                continue
            if any(part in active_rules.ignored_directories for part in path.relative_to(root).parts):
                continue
            findings.extend(scan_file(path, root, active_rules))
    return findings


def format_finding(finding: Finding, root: Path) -> str:
    """Формирует диагностику с путём, строкой, символом и требуемым видом шапки."""

    relative = finding.path.relative_to(root).as_posix()
    return f"{relative}:{finding.line}: {finding.symbol}: требуется русская шапка ({finding.kind})"


def main() -> int:
    """Печатает точные диагностики и возвращает ненулевой код при отсутствии шапки."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--config", type=Path, default=DEFAULT_RULES)
    arguments = parser.parse_args()
    rules = load_rules(arguments.config)
    findings = collect_findings(arguments.root.resolve(), rules)
    for finding in findings:
        print(format_finding(finding, arguments.root.resolve()))
    if findings:
        print(f"Найдено объявлений без шапок: {len(findings)}", file=sys.stderr)
        return 1
    print("Русские шапки C++ проверены.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
