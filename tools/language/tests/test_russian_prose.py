"""Проверяет извлечение русской прозы и диагностику запрещённых терминов."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


LANGUAGE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LANGUAGE_DIR))

from check_russian_prose import collect_violations, load_rules  # noqa: E402


class RussianProseCheckerTests(unittest.TestCase):
    """Проверяет поддерживаемые источники прозы на временном дереве файлов."""

    def setUp(self) -> None:
        """Создаёт минимальные правила для одного изолированного теста."""

        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        rules = {
            "allowedLatinTerms": ["AIPackaging", "API", "Python", "JSON"],
            "ignoredDirectories": ["build"],
            "prohibitedTerms": {"dataset": "набор данных", "датасет": "набор данных"},
            "scanRoots": ["docs", "src", "python", "schemas"],
        }
        rules_path = self.root / "rules.json"
        rules_path.write_text(json.dumps(rules), encoding="utf-8")
        self.rules = load_rules(rules_path)

    def tearDown(self) -> None:
        """Освобождает временное дерево текущего теста."""

        self.temporary.cleanup()

    def _write(self, relative: str, content: str) -> None:
        """Записывает один тестовый файл в настроенную область проверки."""

        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def _terms(self) -> list[str]:
        """Возвращает найденные термины после проверки временного дерева."""

        return [violation.term for violation in collect_violations(self.root, self.rules)]

    def test_rejects_markdown_cpp_python_and_schema_prose(self) -> None:
        """Нарушение обнаруживается во всех обязательных видах проектной прозы."""

        self._write("docs/readme.md", "Этот dataset опубликован.\n")
        self._write("src/example.cpp", "/// Проверяет dataset.\n")
        self._write("python/example.py", '"""Проверяет датасет."""\n')
        self._write("schemas/example.json", '{\n  "title": "AIPackaging dataset"\n}\n')
        self.assertGreaterEqual(self._terms().count("dataset"), 3)
        self.assertIn("датасет", self._terms())

    def test_accepts_code_spans_fences_and_official_names(self) -> None:
        """Машинные литералы и официальные названия не считаются нарушениями."""

        self._write(
            "docs/readme.md",
            "AIPackaging использует Python и JSON.\n"
            "Поле `dataset` является частью API.\n"
            "```json\n{\"dataset\": true}\n```\n",
        )
        self.assertEqual(self._terms(), [])

    def test_rejects_user_facing_python_help(self) -> None:
        """Справка командной строки проверяется как человекочитаемый текст."""

        self._write(
            "python/example.py",
            'parser.add_argument("--input", help="load dataset from disk")\n',
        )
        self.assertIn("dataset", self._terms())

    def test_rejects_user_facing_qt_strings(self) -> None:
        """Строки Qt для интерфейса проверяются, а машинные литералы не затрагиваются."""

        self._write(
            "src/example.cpp",
            'label->setText(tr("Open dataset"));\n'
            'auto title = QCoreApplication::translate("Widget", "Dataset settings");\n'
            'auto objectName = QStringLiteral("datasetWidget");\n',
        )
        self.assertEqual(self._terms().count("dataset"), 2)


if __name__ == "__main__":
    unittest.main()
