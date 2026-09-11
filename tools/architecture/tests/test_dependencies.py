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

    def test_rejects_learning_to_json_include(self) -> None:
        """Learning не должен получать доступ к сериализации через публичный заголовок."""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src/solver/learning/example.cpp"
            header = root / "src/solver/include/aipackaging/nesting/polygon_io.h"
            source.parent.mkdir(parents=True)
            header.parent.mkdir(parents=True)
            source.write_text("#include <aipackaging/nesting/polygon_io.h>\n", encoding="utf-8")
            header.write_text("#pragma once\n", encoding="utf-8")
            rules = self._rules(
                {"learning": "src/solver/learning"},
                {"learning": ["learning"]},
                {"src/solver/include/aipackaging/nesting/polygon_io.h": "json"},
            )
            violations = self._check(root, rules)
            self.assertEqual(1, len(violations))
            self.assertIn("learning не может включать json", violations[0].message)

    def test_rejects_search_to_json_include(self) -> None:
        """Search runtime не должен получать доступ к wire-сериализации."""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "src/solver/search/example.cpp"
            header = root / "src/solver/include/aipackaging/nesting/grid_io.h"
            source.parent.mkdir(parents=True)
            header.parent.mkdir(parents=True)
            source.write_text("#include <aipackaging/nesting/grid_io.h>\n", encoding="utf-8")
            header.write_text("#pragma once\n", encoding="utf-8")
            rules = self._rules(
                {"search": "src/solver/search"},
                {"search": ["search"]},
                {"src/solver/include/aipackaging/nesting/grid_io.h": "json"},
            )
            violations = self._check(root, rules)
            self.assertEqual(1, len(violations))
            self.assertIn("search не может включать json", violations[0].message)

    def test_rejects_external_libraries_outside_owner(self) -> None:
        """GridCore и PolygonCore не могут напрямую включать чужие внешние библиотеки."""

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            grid = root / "src/solver/grid/example.cpp"
            polygon = root / "src/solver/polygon/example.cpp"
            grid.parent.mkdir(parents=True)
            polygon.parent.mkdir(parents=True)
            grid.write_text("#include <clipper2/clipper.h>\n", encoding="utf-8")
            polygon.write_text("#include <nlohmann/json.hpp>\n", encoding="utf-8")
            rules = self._rules(
                {"grid_core": "src/solver/grid", "polygon_core": "src/solver/polygon"},
                {"grid_core": ["grid_core"], "polygon_core": ["polygon_core"]},
                {},
            )
            violations = self._check(root, rules)
            self.assertEqual(2, len(violations))
            self.assertTrue(any("Clipper2" in item.message for item in violations))
            self.assertTrue(any("nlohmann/json" in item.message for item in violations))

    def _check(self, root: Path, rules: dict):
        """Сохраняет минимальные правила fixture и запускает общий checker."""

        rules_path = root / "rules.json"
        rules_path.write_text(json.dumps(rules), encoding="utf-8")
        return check_repository(root, rules_path)

    def _rules(self, layers: dict, allowed: dict, owners: dict) -> dict:
        """Создаёт минимальный полный набор правил для отрицательного self-test."""

        return {
            "version": 1,
            "layers": layers,
            "headerOwners": owners,
            "allowedInternalDependencies": allowed,
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


if __name__ == "__main__":
    unittest.main()
