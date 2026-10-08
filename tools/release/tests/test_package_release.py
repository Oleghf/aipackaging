"""Проверяет состав и воспроизводимость упаковки без модели и сборки продукта."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("package_release", Path(__file__).parents[1] / "package_release.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackageTests(unittest.TestCase):
    """Проверяет контроль целостности и отказ от замены чужих результатов."""

    def test_deterministic_zip_and_tamper_detection(self):
        """Две упаковки совпадают; изменение файла либо добавление мусора отклоняется."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "stage"
            stage.mkdir()
            payload = stage / "руководство.md"
            payload.write_text("Тестовая справка\n", encoding="utf-8")
            manifest = {"files": {payload.name: PACKAGE.digest(payload)}}
            (stage / "release-manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            PACKAGE.verify_stage(stage)
            first, second = root / "one.zip", root / "two.zip"
            PACKAGE.write_zip(stage, first)
            PACKAGE.write_zip(stage, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with self.assertRaises(FileExistsError):
                PACKAGE.write_zip(stage, first)
            extra = stage / "unexpected.log"
            extra.write_text("лишний файл", encoding="utf-8")
            with self.assertRaises(ValueError):
                PACKAGE.verify_stage(stage)
            extra.unlink()
            payload.write_text("повреждено", encoding="utf-8")
            with self.assertRaises(ValueError):
                PACKAGE.verify_stage(stage)

    def test_copy_rejects_directory_and_missing_source(self):
        """Отсутствующий файл и каталог не попадают в поставку."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for source in (root, root / "missing"):
                with self.assertRaises(ValueError):
                    PACKAGE.copy_file(source, root / "output")

    def test_manifest_cannot_escape_stage(self):
        """Путь за пределами комплекта не принимается даже с верным хешем."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage = root / "stage"
            stage.mkdir()
            outside = root / "outside"
            outside.write_bytes(b"data")
            (stage / "release-manifest.json").write_text(
                json.dumps({"files": {"../outside": PACKAGE.digest(outside)}}), encoding="utf-8")
            with self.assertRaises(ValueError):
                PACKAGE.verify_stage(stage)


if __name__ == "__main__":
    unittest.main()
