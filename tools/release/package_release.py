"""Собирает локальный ZIP-кандидат по явному перечню, не очищая прежние сборки и артефакты."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MODEL_IDENTITY = "2c792b5b63b62c3b2f74207f9842300dc3cfc580c9655a15f98a3d850a23bb33"


def digest(path: Path) -> str:
    """Вычисляет SHA-256 файла без изменения содержимого."""
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def copy_file(source: Path, destination: Path) -> None:
    """Копирует только обычный существующий файл; ссылки и каталоги отклоняются."""
    if source.is_symlink() or not source.is_file():
        raise ValueError(f"Ожидался обычный файл: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def verify_stage(stage: Path) -> None:
    """Проверяет полный состав и контрольные суммы; незаявленные файлы считаются ошибкой."""
    manifest = json.loads((stage / "release-manifest.json").read_text(encoding="utf-8"))
    expected = manifest["files"]
    actual = {p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file()}
    if actual != set(expected) | {"release-manifest.json"}:
        raise ValueError("Состав поставки отличается от манифеста")
    for name, sha256 in expected.items():
        path = stage / name
        if path.is_symlink() or not path.resolve().is_relative_to(stage.resolve()) or digest(path) != sha256:
            raise ValueError(f"Нарушена целостность поставки: {name}")


def write_zip(stage: Path, archive: Path) -> None:
    """Создаёт новый архив со стабильным порядком и временем записей; прежний архив не заменяет."""
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo(path.relative_to(stage).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                output.writestr(info, path.read_bytes())


def package(build: Path, qt: Path, crt: Path, output: Path) -> Path:
    """Проверяет Release и модель, создаёт отдельный каталог поставки и ZIP без обучения и сетевых обращений."""
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache or "BUILD_ONNX_BACKEND:BOOL=ON" not in cache:
        raise ValueError("Нужна отдельная сборка Release с ONNX")
    version = re.search(r'project\("AIPackaging" VERSION ([0-9.]+)', (ROOT / "CMakeLists.txt").read_text()).group(1)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip())
    if dirty:
        raise ValueError("Перед упаковкой требуется чистое рабочее дерево")
    stamp = json.loads((build / "release-build.json").read_text(encoding="utf-8"))
    if stamp != {"version": version, "revision": revision[:12]}:
        raise ValueError("Сборка относится к другой редакции; повторите конфигурацию и сборку Release")
    model = ROOT / "artifacts/models/polygon-policy-v1"
    cli = build / "src/cli/AIPackaging_Cli.exe"
    result = subprocess.check_output([str(cli), "validate-model", "--input", str(model)], text=True, encoding="utf-8")
    if MODEL_IDENTITY not in result:
        raise ValueError("Не совпадает идентичность замороженной модели")
    output.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="candidate-", dir=output))
    copy_file(build / "src/AIPackaging.exe", stage / "AIPackaging.exe")
    copy_file(cli, stage / cli.name)
    copy_file(build / "src/aipackaging.ico", stage / "aipackaging.ico")
    for name in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"):
        copy_file(qt / "bin" / name, stage / name)
    # Эти зависимости выбирает штатный `windeployqt` при сборке, но в ZIP попадает только явный перечень.
    for name in ("d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll", "opengl32sw.dll"):
        copy_file(build / "src" / name, stage / name)
    for name in ("platforms/qwindows.dll", "styles/qmodernwindowsstyle.dll", "imageformats/qico.dll"):
        copy_file(qt / "plugins" / name, stage / name)
    for name in ("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll",
                 "msvcp140_codecvt_ids.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcruntime140_threads.dll"):
        copy_file(crt / name, stage / name)
    ort = ROOT / "build/dependencies/onnxruntime-win-x64-1.29.0"
    copy_file(ort / "lib/onnxruntime.dll", stage / "onnxruntime.dll")
    for name in ("metadata.json", "encoder.onnx", "placement-head.onnx"):
        copy_file(model / name, stage / "models/polygon-policy-v1" / name)
    for name in ("index", "installation", "quick-start", "editor", "dxf", "documents", "modes", "troubleshooting"):
        copy_file(ROOT / f"docs/user/{name}.md", stage / f"docs/user/{name}.md")
    examples = ("polygon/problem-small.json", "polygon/problem-concave.json", "polygon/problem-curves-and-hole.json",
                "dxf/square-mm.dxf", "dxf/bulge-inch.dxf", "dxf/spline-and-note.dxf",
                "editor/demo-part-with-hole.aipdraft.json", "editor/demo-open-path.aipdraft.json")
    for name in examples:
        copy_file(ROOT / "examples" / name, stage / "examples" / name)
    copy_file(ROOT / "docs/third-party-notices.md", stage / "THIRD_PARTY_NOTICES.md")
    (stage / "START-HERE.md").write_text(
        "# AIPackaging\n\nЛокальный кандидат: приёмка не завершена.\n\n"
        "Запустите AIPackaging.exe. Клавиша F1 открывает автономную справку.\n\n"
        "[Установка и обновление](docs/user/installation.md).\n", encoding="utf-8")
    for name in ("LICENSE", "ThirdPartyNotices.txt"):
        copy_file(ort / name, stage / "licenses/onnxruntime" / name)
    for name in ("LICENSE", "COPYING.txt", "Copyright.txt", "LICENSE.FDL"):
        copy_file(qt.parent.parent / "Licenses" / name, stage / "licenses/qt" / name)
    copy_file(qt / "sbom/qtbase-6.9.3.spdx.json", stage / "licenses/qt/qtbase-6.9.3.spdx.json")
    qt_docs = qt.parent.parent / "Docs/Qt-6.9.3"
    for name in ("lgpl", "gpl"):
        copy_file(qt_docs / f"qtdoc/{name}.html", stage / f"licenses/qt/{name}.html")
    attributions = {
        "qtcore": ("blake2", "doubleconversion", "easing", "md4", "md5", "pcre2-sljit", "pcre2",
                   "rfc6234", "sha1", "sha3-endian", "sha3-keccak", "siphash", "tika-mimetypes",
                   "tinycbor", "unicode-character-database", "unicode-cldr", "zlib"),
        "qtgui": ("aglfn", "d3d12memoryallocator", "dejayvu", "emoji-segmenter", "freetype-bdf",
                  "freetype-pcf", "freetype-zlib", "freetype", "grayraster", "harfbuzz-ng",
                  "icc-srgb-color-profile", "libjpeg", "libpng", "md4c", "opengl-es2-headers",
                  "opengl-headers", "pixman", "rhi-miniengine-d3d12-mipmap", "smooth-scaling-algorithm",
                  "vulkan-xml-spec", "vulkanmemoryallocator", "webgradients", "wintab"),
    }
    for module, names in attributions.items():
        for name in names:
            filename = f"{module}-attribution-{name}.html"
            copy_file(qt_docs / module / filename, stage / "licenses/qt" / filename)
    copy_file(ROOT / "build/windows-headless-tests/_deps/clipper2-src/LICENSE", stage / "licenses/clipper2/LICENSE")
    copy_file(ROOT / "build/windows-headless-tests/_deps/nlohmann_json-src/LICENSE.MIT", stage / "licenses/nlohmann-json/LICENSE.MIT")
    files = {p.relative_to(stage).as_posix(): digest(p) for p in sorted(stage.rglob("*")) if p.is_file()}
    manifest = {"version": version, "revision": revision, "status": "local-candidate-not-accepted",
                "platform": "windows-x64", "qt": "6.9.3", "onnxRuntime": "1.29.0",
                "modelIdentity": MODEL_IDENTITY, "files": files}
    (stage / "release-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    verify_stage(stage)
    archive = output / f"AIPackaging-{version}-windows-x64.zip"
    write_zip(stage, archive)
    symbols = output / f"symbols-{revision[:12]}"
    for source in (build / "src/AIPackaging.pdb", build / "src/cli/AIPackaging_Cli.pdb"):
        copy_file(source, symbols / source.name)
    archive.with_suffix(".zip.sha256").write_text(digest(archive) + "  " + archive.name + "\n", encoding="ascii")
    print(f"Каталог кандидата: {stage}\nАрхив: {archive}\nSHA-256: {digest(archive)}")
    return archive


def main() -> int:
    """Разбирает параметры упаковки; ошибка не удаляет исходные или ранее подготовленные файлы."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--crt-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    package(args.build_dir.resolve(), args.qt_root.resolve(), args.crt_dir.resolve(), args.output_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
