# AIPackaging

AIPackaging — исследовательский проект нейросетевой системы решения задачи
двумерной упаковки при раскрое листовых материалов. Целевая система должна
работать с деталями произвольной формы, включая криволинейные границы, вырезы и
отверстия.

Репозиторий содержит настольный клеточный прототип и независимое headless-ядро
полигонального раскроя. Desktop-приложение предоставляет отдельные клеточную и
полигональную вкладки; обученная M3-политика пока остаётся клеточной.

## Направление разработки

Планируется гибридный решатель:

1. геометрическое ядро импортирует и нормализует детали;
2. оно формирует допустимые варианты размещения и маску действий;
3. нейросетевая политика выбирает деталь, ориентацию и вариант размещения;
4. точный валидатор гарантирует геометрическую корректность результата;
5. приложение вычисляет метрики и экспортирует раскладку.

Так нейросеть отвечает за глобальные решения, а геометрическая допустимость
остаётся детерминированной. Решение описано в
[ADR-0001](docs/decisions/0001-hybrid-neural-solver.md).

Первые эксперименты обучения проводятся на существующей клеточной модели.
Полигональная геометрия вводится после появления воспроизводимой среды,
baseline-алгоритмов, метрик и цикла обучения.

## Текущие возможности

- интерактивный редактор и сцена клеточной упаковки;
- ручное размещение, перемещение, поворот, undo и redo;
- загрузка и сохранение пулов клеточных фигур;
- проверка границ поля и занятых клеток;
- детерминированное автоматическое размещение first-fit;
- независимое от Qt клеточное solver-ядро с пятью baseline-алгоритмами;
- headless CLI и версионированные JSON-контракты задачи и решения;
- пошаговая обучаемая среда с постоянным action space и reward v1;
- Python 3.12–3.13 bindings и воспроизводимый генератор baseline-траекторий;
- иерархическая PyTorch actor-critic policy, BC/PPO pipeline и ONNX-экспорт;
- neural best-of и безопасный hybrid с точной post-validation;
- strict polygon JSON, line/arc/Bézier и нормализация в `int64` микроны;
- динамические NFP-кандидаты, полигональные baseline и точный валидатор;
- Python `PolygonNestingEnv` и воспроизводимый polygon smoke dataset;
- production `polygon_dataset` v2 с двумя tiers, family isolation, resume и
  benchmark пяти замороженных baseline;
- асинхронная desktop-вкладка polygon_problem с прогрессом, отменой, метриками и
  сохранением независимо проверенного polygon_solution;
- модульные тесты доменного и прикладного слоёв.

Текущие ограничения: полигональный GUI работает в режиме просмотра без
редактора, нет production DXF/SVG, вложения в отверстия и проверенного
полигонального checkpoint. Текущий executable нельзя
описывать как готовую промышленную CAD/CAM-систему.

## Сборка

Требования:

- CMake 3.27 или новее;
- компилятор с поддержкой C++20;
- Qt 6 с компонентами `Core` и `Widgets`;
- Git для загрузки закреплённой версии `AIPackaging_Math`.

Windows Debug:

```powershell
$qtDir = "C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6" # Укажите установленный комплект Qt.
cmake --preset windows-debug -DQt6_DIR="$qtDir"
cmake --build --preset build-windows-debug
```

Сборка и запуск тестов в Windows:

```powershell
$qtDir = "C:/Qt/6.9.3/msvc2022_64/lib/cmake/Qt6" # Укажите установленный комплект Qt.
cmake --preset windows-tests -DQt6_DIR="$qtDir"
cmake --build --preset build-windows-tests
ctest --test-dir build/windows-tests -C Debug --output-on-failure
```

Для Linux и macOS определены аналогичные presets. Вместо передачи `Qt6_DIR`
можно скопировать `CMakeUserPresets.example.json` в игнорируемый Git файл
`CMakeUserPresets.json` и указать в нём локальный путь к Qt. При первой
конфигурации может потребоваться сеть для загрузки зависимостей.

Headless solver и тесты без Qt:

```powershell
cmake --preset windows-headless-tests
cmake --build --preset build-windows-headless-tests
ctest --test-dir build/windows-headless-tests --output-on-failure
```

Пример запуска CLI:

```powershell
build/windows-headless-tests/src/cli/AIPackaging_Cli.exe solve `
  --input examples/grid/problem-small.json `
  --output build/solution.json `
  --solver area-left-bottom
```

Схемы находятся в `schemas/`, а распространяемые примеры — в `examples/grid/`.
Формат сцены desktop-приложения `aipackaging.packing_scene` от solver-форматов
не зависит и в M1 не менялся.

Полигональный CLI использует ту же команду и автоматически распознаёт формат:

```powershell
build/windows-headless-tests/src/cli/AIPackaging_Cli.exe solve `
  --input examples/polygon/problem-small.json `
  --output build/polygon-solution.json `
  --solver area-left-bottom
build/windows-headless-tests/src/cli/AIPackaging_Cli.exe validate `
  --problem examples/polygon/problem-small.json `
  --solution build/polygon-solution.json
```

В desktop откройте вкладку «Полигональный раскрой», выберите
`polygon_problem` v1 и baseline. Полные и обычные partial-решения можно
сохранить после автоматической повторной проверки; отменённый partial доступен
только для просмотра.

Python-среда и датасет:

```powershell
python -m pip install -e ".[dev]"
python -m pytest
python -m aipackaging_ml generate-dataset --output artifacts/datasets/grid-v1
python -m aipackaging_ml verify-dataset artifacts/datasets/grid-v1
python -m aipackaging_ml generate-polygon-dataset `
  --output artifacts/datasets/polygon-v2 --workers 4 --resume
python -m aipackaging_ml verify-polygon-dataset artifacts/datasets/polygon-v2
python -m aipackaging_ml benchmark-polygon-baselines `
  --dataset artifacts/datasets/polygon-v2 `
  --output artifacts/benchmarks/polygon-v2-validation.json
```

Grid-генератор создаёт 768 задач. Polygon v2 создаёт 384 задачи: 256/64/64,
поровну между `small` и `medium`. `--smoke` создаёт 12 коротких задач — по две
scale-вариации каждого split и tier. Worker count и история `--resume` не меняют
опубликованные bytes.

Канонический polygon dataset v2 заморожен на revision `1baf53a8c1f0`.
SHA-256 manifest:
`747ADE28A858DE2F1D484CDF6C942DF07CB59AF017EEB886F8C18D9105422C44`.
Фактические размеры, coverage и результаты validation benchmark приведены в
[отчёте M6.1](docs/experiments/m6-1-polygon-dataset-v2-2026-09-13.md).

CUDA-обучение M3 можно выполнять в WSL2 или непосредственно в проверенной
Windows-среде. Подготовка и команды запуска описаны в
[руководстве M3](docs/m3-training.md). Базовый пакет не зависит от PyTorch;
зависимости обучения устанавливаются через
`python -m pip install -e ".[dev,train]"`.

## Структура репозитория

```text
src/core/domain/     Клеточная доменная модель и геометрия
src/core/app/        Сценарии, команды, валидация и стратегии упаковки
src/core/contract/   Общие события и интерфейсы
src/gui/             Пользовательский интерфейс Qt
src/solver/          Headless клеточная/полигональная среда, baseline и JSON I/O
src/cli/             Командная строка исследовательского solver
src/python/          Низкоуровневые pybind11 bindings
python/aipackaging_ml/ Python API, модульные dataset pipelines, обучение и ONNX
tests/               Автоматические тесты C++
examples/            Примеры клеточных фигур
schemas/             JSON Schema публичных форматов
docs/                Цели, архитектура, качество и план разработки
```

## Правила разработки

- Перед изменениями прочитайте [AGENTS.md](AGENTS.md).
- Применяйте `.clang-format` только к изменяемым файлам; не форматируйте весь
  репозиторий вместе с функциональной задачей.
- Используйте `.clang-tidy` как постепенный контроль качества.
- Изменения геометрии, размещения, сериализации и целевой функции должны
  сопровождаться тестами и обновлением документации.

Навигация по документации и известным проблемам находится в
[docs/README.md](docs/README.md).

## Лицензия

Лицензия проекта пока не выбрана. До добавления файла `LICENSE` код нельзя
считать открытым или разрешённым к распространению.
