"""Отрицательные тесты проверки документации AIPackaging."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import check_documentation  # noqa: E402


class DocumentationCheckerTests(unittest.TestCase):
    """Проверяет обнаружение повреждённых ссылок и ключевых фактов."""

    def setUp(self) -> None:
        """Создаёт минимальный согласованный репозиторий для каждого теста."""

        self.temporary = tempfile.TemporaryDirectory(prefix="aipackaging-docs-")
        self.root = Path(self.temporary.name)
        self._write("CMakeLists.txt", 'project("AIPackaging" VERSION 1.2.3 LANGUAGES CXX)\n')
        self._write("pyproject.toml", '[project]\nversion = "1.2.3"\n')
        self._write("python/aipackaging_ml/__init__.py", '__version__ = "1.2.3"\n')
        self._write("README.md", "# Проект\n\nВерсия `1.2.3`.\n")
        self._write(
            "docs/README.md",
            "# Документация\n\nВерсия `1.2.3`. [Состояние](current-state.md).\n",
        )
        self._write(
            "docs/current-state.md",
            "# Состояние\n\nВерсия `1.2.3`. Этап U1, далее M7.\n\n## Детали\n",
        )
        self.rules = self.root / "rules.json"
        self._write_rules()

    def tearDown(self) -> None:
        """Удаляет временный репозиторий после теста."""

        self.temporary.cleanup()

    def _write(self, relative: str, content: str) -> None:
        """Записывает тестовый файл относительно временного корня."""

        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")

    def _write_rules(self, *, required_text: str = "Этап U1, далее M7.", hash_value: str = "a" * 64) -> None:
        """Записывает правила с выбранным фактом и контрольным хешем."""

        self._write(
            "rules.json",
            json.dumps(
                {
                    "requiredIndexLinks": ["docs/current-state.md"],
                    "removedDocuments": ["old-plan.md"],
                    "versionDocuments": ["README.md", "docs/README.md", "docs/current-state.md"],
                    "requiredText": {"docs/current-state.md": [required_text]},
                    "hashes": {
                        "example": {
                            "value": hash_value,
                            "files": ["docs/current-state.md"],
                        }
                    },
                },
                ensure_ascii=False,
            ),
        )
        state = self.root / "docs/current-state.md"
        text = state.read_text(encoding="utf-8")
        if hash_value not in text:
            state.write_text(text + f"\n`{hash_value}`\n", encoding="utf-8")

    def _errors(self) -> list[str]:
        """Возвращает диагностику проверки временного репозитория."""

        return check_documentation.check(self.root, self.rules)

    def test_accepts_consistent_documentation(self) -> None:
        """Принимает согласованные ссылки, версии, этапы и хеши."""

        self.assertEqual([], self._errors())

    def test_rejects_missing_file_and_anchor(self) -> None:
        """Отклоняет отсутствующие локальный файл и заголовок Markdown."""

        self._write(
            "README.md",
            "# Проект\n\nВерсия `1.2.3`. [Нет файла](docs/missing.md). "
            "[Нет заголовка](docs/current-state.md#нет-раздела).\n",
        )
        errors = "\n".join(self._errors())
        self.assertIn("цель ссылки не существует", errors)
        self.assertIn("заголовок ссылки не существует", errors)

    def test_rejects_version_mismatch(self) -> None:
        """Отклоняет несовпадение машинных версий и версии в README."""

        self._write("pyproject.toml", '[project]\nversion = "9.9.9"\n')
        self.assertIn("версии проекта не совпадают", "\n".join(self._errors()))

    def test_rejects_stale_milestone(self) -> None:
        """Отклоняет устаревший текущий или следующий этап."""

        self._write_rules(required_text="Этап U1, далее M7.")
        path = self.root / "docs/current-state.md"
        path.write_text(read_without(path, "Этап U1, далее M7."), encoding="utf-8")
        self.assertIn("отсутствует обязательный факт", "\n".join(self._errors()))

    def test_rejects_wrong_hash_and_removed_reference(self) -> None:
        """Отклоняет неверный хеш и упоминание удалённого документа."""

        path = self.root / "docs/current-state.md"
        text = path.read_text(encoding="utf-8").replace("a" * 64, "b" * 64)
        path.write_text(text + "\nСм. old-plan.md.\n", encoding="utf-8")
        errors = "\n".join(self._errors())
        self.assertIn("отсутствует ожидаемый SHA-256", errors)
        self.assertIn("удалённого документа", errors)


def read_without(path: Path, fragment: str) -> str:
    """Читает файл и удаляет из него заданный фрагмент."""

    return path.read_text(encoding="utf-8").replace(fragment, "Этап M6, далее U1.")


if __name__ == "__main__":
    unittest.main()
