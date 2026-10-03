"""Проверяет учёт изменённых и новых исходников без привязки к истории Git."""

import importlib.util
import tempfile
import unittest
import sys
from types import SimpleNamespace
from pathlib import Path
from importlib.machinery import EXTENSION_SUFFIXES


spec = importlib.util.spec_from_file_location("native_fingerprint", Path(__file__).parents[1] / "native_fingerprint.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
sys.modules["native_fingerprint"] = module
check_spec = importlib.util.spec_from_file_location("check_native_freshness", Path(__file__).parents[1] / "check_native_freshness.py")
checker = importlib.util.module_from_spec(check_spec)
check_spec.loader.exec_module(checker)


class NativeFingerprintTests(unittest.TestCase):
    """Проверяет изменения входов сборки в изолированном временном дереве."""

    def test_rejects_legacy_mismatch_and_missing_origin(self):
        """Старый модуль, иная версия и отпечаток не допускаются к выполнению pytest."""
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        binary = Path(temporary.name) / ("module" + EXTENSION_SUFFIXES[0])
        binary.touch()
        native = SimpleNamespace(__file__=str(binary), __version__="0.12.0")
        self.assertFalse(checker.matches_native(native, "0.12.0", "0.12.0", "digest"))
        native.__source_fingerprint__ = "digest"
        self.assertTrue(checker.matches_native(native, "0.12.0", "0.12.0", "digest"))
        self.assertFalse(checker.matches_native(native, "0.11.0", "0.12.0", "digest"))
        self.assertFalse(checker.matches_native(native, "0.12.0", "0.12.0", "changed"))
        binary.unlink()
        self.assertFalse(checker.matches_native(native, "0.12.0", "0.12.0", "digest"))
        native.__file__ = None
        self.assertFalse(checker.matches_native(native, "0.12.0", "0.12.0", "digest"))

    def test_source_changes_but_documentation_does_not(self):
        """Новый C++ меняет отпечаток, а редактура README и переводов строк — нет."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ("CMakeLists.txt", "src/CMakeLists.txt", "pyproject.toml", "tools/quality/native_fingerprint.py"):
                target = root / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("fixture\n", encoding="utf-8")
            before = module.fingerprint(root)
            (root / "README.md").write_text("изменение документации", encoding="utf-8")
            self.assertEqual(before, module.fingerprint(root))
            (root / "src/solver").mkdir()
            source = root / "src/solver/new.cpp"
            source.write_bytes(b"int value;\r\n")
            added = module.fingerprint(root)
            self.assertNotEqual(before, added)
            source.write_bytes(b"int value;\n")
            self.assertEqual(added, module.fingerprint(root))
            source.write_bytes(b"int changed;\n")
            self.assertNotEqual(added, module.fingerprint(root))
