"""Проверяет обнаружение отсутствующих русских шапок C++."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


LANGUAGE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LANGUAGE_DIR))

from check_cpp_headers import collect_findings, format_finding, load_rules  # noqa: E402


class CppHeaderCheckerTests(unittest.TestCase):
    """Проверяет обязательные и допустимые конструкции на временном дереве."""

    def setUp(self) -> None:
        """Создаёт минимальную область проверки без исключений."""

        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        rules_path = self.root / "rules.json"
        rules_path.write_text(
            json.dumps(
                {
                    "scanRoots": ["src"],
                    "ignoredDirectories": ["external"],
                    "excludedFiles": [],
                    "exceptions": [],
                }
            ),
            encoding="utf-8",
        )
        self.rules = load_rules(rules_path)

    def tearDown(self) -> None:
        """Удаляет временные исходники проверки."""

        self.temporary.cleanup()

    def _find(self, name: str, content: str) -> list[str]:
        """Записывает исходник и возвращает имена найденных символов."""

        path = self.root / "src" / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        return [finding.symbol for finding in collect_findings(self.root, self.rules)]

    def test_rejects_class_header_definition_and_deleted_function(self) -> None:
        """Класс, объявление, определение и удалённая функция требуют шапок."""

        symbols = self._find(
            "sample.h",
            "class Missing {\npublic:\n  void call();\n  Missing(const Missing &) = delete;\n};\n",
        )
        self.assertIn("Missing", symbols)
        self.assertIn("call", symbols)
        self.assertIn("Missing", symbols)
        symbols += self._find("sample.cpp", "int compute(int value) { return value; }\n")
        self.assertIn("compute", symbols)

    def test_diagnostic_contains_path_line_symbol_and_requirement(self) -> None:
        """Диагностика однозначно указывает место, символ и требуемый вид шапки."""

        self._find("missing.cpp", "int compute() { return 1; }\n")
        finding = collect_findings(self.root, self.rules)[0]
        diagnostic = format_finding(finding, self.root)
        self.assertIn("src/missing.cpp:1", diagnostic)
        self.assertIn("compute", diagnostic)
        self.assertIn("русская шапка", diagnostic)

    def test_accepts_templates_operators_macros_lambdas_and_external_code(self) -> None:
        """Документированные сложные конструкции принимаются, а макросы, лямбды и внешнее дерево не проверяются."""

        symbols = self._find(
            "accepted.h",
            "/// Описывает шаблон.\n"
            "template<class T>\nstruct Box {\n"
            "  /// Сравнивает значения.\n"
            "  bool operator==(const Box &) const = default;\n"
            "  /// Возвращает значение по индексу.\n"
            "  T operator[](int index) const;\n"
            "};\n"
            "DECLARE_WIDGET(name)\n",
        )
        external = self.root / "src" / "external" / "foreign.cpp"
        external.parent.mkdir(parents=True)
        external.write_text("int foreign() { return 0; }\n", encoding="utf-8")
        lambda_path = self.root / "src" / "lambda.cpp"
        lambda_path.write_text("auto action = []() { return 1; };\n", encoding="utf-8")
        symbols = [finding.symbol for finding in collect_findings(self.root, self.rules)]
        self.assertEqual(symbols, [])


if __name__ == "__main__":
    unittest.main()
