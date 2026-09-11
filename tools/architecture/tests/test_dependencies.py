"""Self-tests архитектурного сканера на заведомо запрещённых зависимостях."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ARCHITECTURE_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ARCHITECTURE_DIR))

from check_dependencies import check_repository  # noqa: E402


class DependencyCheckerTests(unittest.TestCase):
    """Проверяет, что сканер действительно отклоняет запрещённые include."""

    def test_rejects_core_to_qt_include(self) -> None:
        """Запрещённый Qt include из nesting Core должен давать ошибку с номером строки."""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src/nesting/core/example.cpp"
            source.parent.mkdir(parents=True)
            source.write_text("#include <QWidget>\n", encoding="utf-8")
            rules = {
                "version": 1,
                "layers": {"nesting_core": "src/nesting/core"},
                "allowedInternalDependencies": {"nesting_core": ["nesting_core"]},
                "includeExceptions": [],
                "externalIncludes": {
                    "qtPrefixes": ["Q", "Qt"],
                    "qtAllowedLayers": [],
                    "nlohmannAllowedFiles": [],
                    "clipperAllowedFiles": [],
                },
                "python": {"roots": [], "torchAllowedFiles": []},
                "ignoredDirectories": ["build"],
            }
            rules_path = root / "rules.json"
            rules_path.write_text(json.dumps(rules), encoding="utf-8")

            violations = check_repository(root, rules_path)

            self.assertEqual(1, len(violations))
            self.assertEqual(1, violations[0].line)
            self.assertIn("Qt include", violations[0].message)


if __name__ == "__main__":
    unittest.main()
