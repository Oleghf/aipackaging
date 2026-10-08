# Разработка и CLI

## Среда и сборка

Пользователю ZIP среда разработки не нужна. Для сборки в Windows нужны
Visual Studio с C++ x64, CMake, Ninja, Qt 6.9.3 MSVC x64 и Python для средств
сборки и контроля. Команды выполняются из корня репозитория в терминале
разработчика x64. Исследовательские зависимости перечислены в `pyproject.toml`;
настольное приложение их не загружает.

```powershell
$env:VSLANG = '1033'
cmake --preset windows-desktop-ci-tests -DQt6_DIR=C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6
cmake --build --preset build-windows-desktop-ci-tests
ctest --test-dir build/windows-desktop-ci-tests --output-on-failure
```

Язык диагностики MSVC фиксируется предустановками. Старую конфигурацию с
неверным префиксом `showIncludes` требуется повторно настроить и полностью
пересобрать. Проверка `ninja -C <каталог> -t deps` должна содержать зависимости
от заголовков. Чистое ядро — `windows-nesting-tests`, прикладной слой без Qt —
`windows-headless-tests`, GUI без ONNX — `windows-desktop-no-onnx-tests`.
Остальные команды — в [качестве](quality.md); ZIP — в [поставке](release.md).

## Командная строка

CLI принимает клеточные и полигональные задачи и решения, но не черновики и
не DXF. Пути Юникода поддерживаются. Примеры из распакованного комплекта:

```powershell
.\AIPackaging_Cli.exe solve --input examples/polygon/problem-small.json --output solution.json --solver area-left-bottom
.\AIPackaging_Cli.exe validate --problem examples/polygon/problem-small.json --solution solution.json
.\AIPackaging_Cli.exe validate-model --input models/polygon-policy-v1
.\AIPackaging_Cli.exe solve --input examples/polygon/problem-small.json --output hybrid.json --solver hybrid --model models/polygon-policy-v1 --seed 42
```

Точную справку выдаёт запуск без аргументов (код завершения 1). Повтор
параметра сохраняет последнее значение. `--seed` задаёт начальное значение
генератора случайных чисел; бюджеты должны быть положительными.

| Код | Значение |
|---|---|
| 0 | Полное решение или успешная проверка |
| 1 | Аргументы, файловая операция либо недоступный режим |
| 2 | Корректное частичное решение |
| 3 | Неподдерживаемый формат, нарушенный контракт, некорректное решение или модель |

Частичное решение не означает размещение всех экземпляров. Результат
проверяется командой `validate` с исходной задачей. Подробности модели —
в [руководстве M6.3](m6-3-model-delivery.md).
