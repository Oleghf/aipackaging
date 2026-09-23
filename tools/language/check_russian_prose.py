#!/usr/bin/env python3
"""Проверяет, что человекочитаемая проза репозитория написана по-русски."""

from __future__ import annotations

import argparse
import ast
import io
import json
import re
import sys
import tokenize
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_RULES = Path(__file__).with_name("russian-prose-rules.json")
LATIN_TOKEN = re.compile(r"[A-Za-z][A-Za-z0-9]*(?:[.+#-][A-Za-z0-9]+)*")
CODE_SPAN = re.compile(r"`[^`]*`")
MARKDOWN_LINK_TARGET = re.compile(r"\]\([^)]*\)")
URL = re.compile(r"https?://\S+")
STAGE_TOKEN = re.compile(
    r"(?:[AMR]\d+(?:\.\d+)?R?(?:-[AMR]?\d+(?:\.\d+)?)?|ADR-\d+|v\d+|x\d+|\d+x\d+|[A-Z])",
    re.IGNORECASE,
)
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".cxx"}
SCRIPT_SUFFIXES = {".cmake", ".sh", ".ps1", ".yml", ".yaml"}
HUMAN_CPP_MARKERS = (
    "fail(",
    "statusText",
    "setText(",
    "setToolTip(",
    "setWindowTitle(",
    "QMessageBox",
    "std::cerr",
    "std::cout",
    "throw ",
    "TEST_CASE(",
    "SECTION(",
    "INFO(",
)
HUMAN_PYTHON_CALLS = {
    "ArgumentParser",
    "Exception",
    "IndexError",
    "KeyError",
    "RuntimeError",
    "TypeError",
    "ValueError",
    "add_argument",
    "add_parser",
    "error",
    "print",
}


@dataclass(frozen=True)
class Rules:
    """Содержит разрешённые названия, запрещённые термины и область проверки."""

    allowed_latin_terms: frozenset[str]
    prohibited_terms: dict[str, str]
    ignored_directories: frozenset[str]
    scan_roots: tuple[str, ...]


@dataclass(frozen=True)
class ProseFragment:
    """Описывает один человекочитаемый фрагмент и его исходную строку."""

    path: Path
    line: int
    text: str


@dataclass(frozen=True)
class Violation:
    """Описывает найденный термин и рекомендуемое исправление."""

    path: Path
    line: int
    term: str
    replacement: str


def load_rules(path: Path = DEFAULT_RULES) -> Rules:
    """Загружает строгую конфигурацию проверки из файла JSON."""

    document = json.loads(path.read_text(encoding="utf-8"))
    return Rules(
        allowed_latin_terms=frozenset(term.casefold() for term in document["allowedLatinTerms"]),
        prohibited_terms={term.casefold(): replacement for term, replacement in document["prohibitedTerms"].items()},
        ignored_directories=frozenset(document["ignoredDirectories"]),
        scan_roots=tuple(document["scanRoots"]),
    )


def _relative(path: Path, root: Path) -> str:
    """Возвращает переносимый относительный путь для диагностического сообщения."""

    return path.relative_to(root).as_posix()


def _is_ignored(path: Path, root: Path, rules: Rules) -> bool:
    """Определяет, принадлежит ли файл исключённому каталогу или двоичному дереву."""

    relative = path.relative_to(root)
    return any(part in rules.ignored_directories for part in relative.parts)


def iter_files(root: Path, rules: Rules) -> Iterable[Path]:
    """Перечисляет собственные текстовые файлы только в настроенной области проекта."""

    for configured in rules.scan_roots:
        candidate = root / configured
        if candidate.is_file():
            yield candidate
            continue
        if not candidate.is_dir():
            continue
        for path in sorted(item for item in candidate.rglob("*") if item.is_file()):
            if not _is_ignored(path, root, rules):
                yield path


def _clean_prose(text: str) -> str:
    """Удаляет из прозы фрагменты кода, адреса и цели ссылок перед проверкой слов."""

    text = CODE_SPAN.sub(" ", text)
    text = MARKDOWN_LINK_TARGET.sub("]", text)
    text = re.sub(r"\\(?:brief|details|param|return|throws?)\b", " ", text)
    text = re.sub(r"\bnoqa\s*:\s*[A-Za-z0-9, -]+", " ", text)
    # Идентификаторы форматов и полей с подчёркиванием являются машинными литералами.
    text = re.sub(r"\b[A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)+\b", " ", text)
    return URL.sub(" ", text)


def markdown_fragments(path: Path) -> list[ProseFragment]:
    """Извлекает текст Markdown вне ограждённых блоков кода."""

    fragments: list[ProseFragment] = []
    in_fence = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            fragments.append(ProseFragment(path, number, _clean_prose(line)))
    return fragments


def _python_call_name(node: ast.Call) -> str:
    """Возвращает простое имя вызываемой Python-функции или метода."""

    if isinstance(node.func, ast.Name):
        return node.func.id
    if isinstance(node.func, ast.Attribute):
        return node.func.attr
    return ""


def _direct_string_fragments(nodes: Iterable[ast.AST], path: Path) -> list[ProseFragment]:
    """Извлекает непосредственные строковые части выбранных выражений Python."""

    fragments: list[ProseFragment] = []
    for node in nodes:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            fragments.append(ProseFragment(path, node.lineno, _clean_prose(node.value)))
        elif isinstance(node, ast.JoinedStr):
            for child in node.values:
                if isinstance(child, ast.Constant) and isinstance(child.value, str):
                    fragments.append(ProseFragment(path, child.lineno, _clean_prose(child.value)))
    return fragments


def python_fragments(path: Path) -> list[ProseFragment]:
    """Извлекает комментарии, строки документации и пользовательские строки Python."""

    source = path.read_text(encoding="utf-8")
    fragments: list[ProseFragment] = []
    try:
        tokens = tokenize.generate_tokens(io.StringIO(source).readline)
        for token in tokens:
            if token.type == tokenize.COMMENT:
                if token.start[0] == 1 and token.string.startswith("#!"):
                    continue
                fragments.append(ProseFragment(path, token.start[0], _clean_prose(token.string.lstrip("# "))))
    except tokenize.TokenError:
        return fragments

    try:
        tree = ast.parse(source)
    except SyntaxError:
        return fragments

    documented_nodes = [tree, *(node for node in ast.walk(tree) if isinstance(node, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)))]
    for node in documented_nodes:
        body = getattr(node, "body", [])
        if body and isinstance(body[0], ast.Expr) and isinstance(body[0].value, ast.Constant) and isinstance(body[0].value.value, str):
            fragments.append(ProseFragment(path, body[0].lineno, _clean_prose(body[0].value.value)))

    for node in ast.walk(tree):
        if isinstance(node, ast.Raise) and node.exc is not None:
            if isinstance(node.exc, ast.Call):
                fragments.extend(_direct_string_fragments(node.exc.args, path))
        if isinstance(node, ast.Call):
            call_name = _python_call_name(node)
            if call_name in {"print", "error"} or call_name.endswith("Error"):
                fragments.extend(_direct_string_fragments(node.args, path))
            if call_name in HUMAN_PYTHON_CALLS:
                human_keywords = (keyword.value for keyword in node.keywords if keyword.arg in {"description", "help"})
                fragments.extend(_direct_string_fragments(human_keywords, path))
    return fragments


def _cpp_quoted_fragments(line: str, path: Path, number: int) -> list[ProseFragment]:
    """Извлекает человекочитаемые строковые литералы из диагностической строки C++."""

    exact_values = re.findall(r'(?<![A-Za-z0-9_])tr\(\s*"((?:\\.|[^"\\])*)"', line)
    exact_values.extend(
        re.findall(
            r'(?<![A-Za-z0-9_])translate\(\s*"(?:\\.|[^"\\])*"\s*,\s*"((?:\\.|[^"\\])*)"',
            line,
        )
    )
    values = re.findall(r'"((?:\\.|[^"\\])*)"', line) if any(marker in line for marker in HUMAN_CPP_MARKERS) else exact_values
    return [ProseFragment(path, number, _clean_prose(value)) for value in values if re.search(r"[A-Za-zА-Яа-яЁё]", value)]


def _looks_like_prose(text: str) -> bool:
    """Отделяет естественный текст от временно закомментированного исходного кода."""

    stripped = text.strip().lstrip("/ ")
    if not stripped:
        return False
    if any(marker in stripped for marker in (";", "{", "}", "::", "#include", "EXPECT_", "TEST(")):
        return False
    if re.match(r"^(?:auto|class|const|enum|for|if|namespace|return|struct|template|using|while)\b", stripped):
        return False
    if re.search(r"[А-Яа-яЁё]", stripped):
        return True
    return len(LATIN_TOKEN.findall(stripped)) >= 3


def _append_cpp_comment(fragments: list[ProseFragment], path: Path, number: int, text: str) -> None:
    """Добавляет комментарий только тогда, когда он похож на естественную прозу."""

    cleaned = _clean_prose(text.strip().lstrip("*/ "))
    if _looks_like_prose(cleaned):
        fragments.append(ProseFragment(path, number, cleaned))


def cpp_fragments(path: Path) -> list[ProseFragment]:
    """Извлекает построчные и блочные комментарии, а также диагностические строки C++."""

    fragments: list[ProseFragment] = []
    in_block = False
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        remainder = line
        if in_block:
            end = remainder.find("*/")
            if end < 0:
                _append_cpp_comment(fragments, path, number, remainder)
                continue
            _append_cpp_comment(fragments, path, number, remainder[:end])
            remainder = remainder[end + 2 :]
            in_block = False
        while "/*" in remainder:
            start = remainder.find("/*")
            end = remainder.find("*/", start + 2)
            if end < 0:
                _append_cpp_comment(fragments, path, number, remainder[start + 2 :])
                in_block = True
                remainder = remainder[:start]
                break
            _append_cpp_comment(fragments, path, number, remainder[start + 2 : end])
            remainder = remainder[:start] + remainder[end + 2 :]
        marker = remainder.find("//")
        if marker >= 0:
            _append_cpp_comment(fragments, path, number, remainder[marker + 2 :])
            remainder = remainder[:marker]
        fragments.extend(_cpp_quoted_fragments(remainder, path, number))
    return fragments


def script_fragments(path: Path) -> list[ProseFragment]:
    """Извлекает комментарии из файлов сборки, сценариев и рабочих процессов."""

    fragments: list[ProseFragment] = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("#") and not stripped.startswith("#!"):
            fragments.append(ProseFragment(path, number, _clean_prose(stripped.lstrip("# "))))
    return fragments


def schema_fragments(path: Path) -> list[ProseFragment]:
    """Извлекает человекочитаемые заголовки и описания схем JSON."""

    fragments: list[ProseFragment] = []
    pattern = re.compile(r'^\s*"(?:title|description)"\s*:\s*"(.*)"\s*,?\s*$')
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = pattern.match(line)
        if match:
            fragments.append(ProseFragment(path, number, _clean_prose(match.group(1))))
    return fragments


def fragments_for(path: Path) -> list[ProseFragment]:
    """Выбирает способ извлечения прозы по типу файла."""

    if path.suffix == ".md":
        return markdown_fragments(path)
    if path.suffix == ".py":
        return python_fragments(path)
    if path.suffix in SOURCE_SUFFIXES:
        return cpp_fragments(path)
    if path.suffix in SCRIPT_SUFFIXES or path.name == "CMakeLists.txt":
        return script_fragments(path)
    if path.suffix == ".json" and "schemas" in path.parts:
        return schema_fragments(path)
    return []


def _term_violations(fragment: ProseFragment, root: Path, rules: Rules) -> list[Violation]:
    """Сопоставляет слова фрагмента с запретами и разрешёнными официальными названиями."""

    violations: list[Violation] = []
    lowered = fragment.text.casefold()
    for term, replacement in rules.prohibited_terms.items():
        if re.search(rf"(?<![\w]){re.escape(term)}(?![\w])", lowered, re.IGNORECASE):
            violations.append(Violation(fragment.path, fragment.line, term, replacement))

    prohibited_latin = {term for term in rules.prohibited_terms if LATIN_TOKEN.fullmatch(term)}
    latin_source = fragment.text
    for allowed in sorted(rules.allowed_latin_terms, key=len, reverse=True):
        latin_source = re.sub(rf"(?<![\w]){re.escape(allowed)}(?![\w])", " ", latin_source, flags=re.IGNORECASE)
    for token in LATIN_TOKEN.findall(latin_source):
        folded = token.casefold()
        is_identifier = (
            "_" in token
            or any(character.isupper() for character in token[1:])
            or (len(token) > 1 and token.isupper())
        )
        if (
            folded in prohibited_latin
            or folded in rules.allowed_latin_terms
            or STAGE_TOKEN.fullmatch(token)
            or is_identifier
        ):
            continue
        violations.append(
            Violation(
                fragment.path,
                fragment.line,
                token,
                "используйте русский термин или оформите точный идентификатор как код",
            )
        )
    return violations


def collect_violations(root: Path, rules: Rules) -> list[Violation]:
    """Проверяет все настроенные файлы и возвращает устойчиво отсортированные нарушения."""

    unique: dict[tuple[str, int, str], Violation] = {}
    for path in iter_files(root, rules):
        for fragment in fragments_for(path):
            for violation in _term_violations(fragment, root, rules):
                key = (_relative(violation.path, root), violation.line, violation.term.casefold())
                unique[key] = violation
    return [unique[key] for key in sorted(unique)]


def main() -> int:
    """Запускает проверку и печатает совместимые с редакторами координаты нарушений."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="корневой каталог проверяемого проекта")
    parser.add_argument("--rules", type=Path, default=DEFAULT_RULES, help="файл правил и терминов JSON")
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    violations = collect_violations(root, load_rules(arguments.rules))
    for violation in violations:
        print(f"{_relative(violation.path, root)}:{violation.line}: '{violation.term}' — {violation.replacement}")
    if violations:
        print(f"Найдено нарушений: {len(violations)}", file=sys.stderr)
        return 1
    print("Проверка русской проектной прозы пройдена.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
