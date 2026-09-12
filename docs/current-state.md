# Текущее состояние

Проверено 2026-09-12.

## Baseline

- Windows Debug приложение собирается MSVC с Qt 6.9.3.
- Desktop и headless CTest targets проверяют существующее приложение и новое
  solver-ядро.
- Проект использует C++20, CMake, Qt 6 и закреплённую Git-ревизию
  `AIPackaging_Math`.
- Код разделён на contract, domain, application и GUI.
- Автоупаковка реализована детерминированным first-fit по клеткам.
- Фигуры сериализуются списками целочисленных координат клеток.
- Независимый `AIPackaging_Solver` поддерживает пять воспроизводимых baseline:
  input-first-fit, две сортировки с left-bottom, seeded random и beam search.
- `AIPackaging_Cli` читает `grid_problem` v1 и сохраняет `grid_solution` v1 с
  независимо проверяемыми placements, objective и метриками.
- `GridLearningEnvironment` предоставляет постоянный каталог действий, mask,
  observation v1, compact rollout API и точный reward v1.
- Python-пакет `aipackaging-ml` предоставляет gym-like API, генератор профилей
  small/medium и verifier `grid_dataset`/`grid_trajectory` v1.
- Канонический датасет содержит 768 задач и 3840 baseline-траекторий; он
  генерируется локально в игнорируемом `artifacts/datasets/grid-v1`.
- Реализован M3 pipeline: иерархическая actor-critic policy, expert BC, PPO,
  multiprocessing rollout, neural/hybrid runner, solution v2 и двухчастный ONNX.
- Реализован M4 polygon-контур: strict JSON, line/arc/Bézier, микронные кольца,
  внешние границы и отверстия, NFP-кандидаты, пять baseline и post-validation.
- `PolygonLearningEnvironment` предоставляет динамические действия и
  raster-observation 4×128×128; Python умеет создавать и проверять smoke dataset.
- Проверенный polygon smoke dataset содержит 24 задачи и 120 baseline-траекторий;
  SHA-256 manifest: `da53ac408c2ec715205d11931cc0690a706ef153791aafe19fb43962b76cbc5f`.
- Desktop M5 содержит отдельную полигональную вкладку: strict загрузка задачи,
  пять baseline, расширенные бюджеты, асинхронный progress/отмена, визуализация
  колец и сохранение только independently validated результата.
- В A2 общие контракты вынесены в `AIPackaging_NestingCore`, а canonical API
  получил prefix `aipackaging/nesting` без изменения wire-форматов.
- В A3 реализация разделена на `SearchContracts`, `GridCore`, `PolygonCore`,
  `Search`, `Json` и `Learning`. `SolverImpl` и `Solver` теперь являются
  INTERFACE compatibility targets для App/GUI до A6.
- Полигональная реализация разделена на normalization, collision/clearance,
  NFP-кандидаты, objective/raster, state и validation; Clipper2 и nlohmann/json
  локализованы в своих implementation targets.
- Canonical `polygon_types.h` больше не зависит от клеточного `gridtypes.h`.
- В A4 grid и polygon baseline используют единый внутренний search runtime для
  ordering, random/beam lifecycle, timeout, cancellation, progress, метрик и
  metadata. Предметные адаптеры сохраняют прежнюю геометрию и objective.
- Grid получил управляемый `runGridProblem`; прежние polygon progress/control
  имена сохранены aliases, а `solveGridProblem`/`solvePolygonProblem` совместимы.
- В A5 общий Python-модуль владеет canonical JSON/JSONL, deterministic gzip,
  SHA-256, shard paths и атомарным cache envelope. Grid и polygon pipeline
  разделены на generation, rollout, replay/verification и manifest assembly.
- Старые Python import paths сохранены фасадами; CLI использует отдельные
  dataset/ML handlers, поэтому generate/verify не импортируют PyTorch.
- В M6.1R поверх границ A5 добавлены production polygon dataset v2, profiles,
  парные scale-семейства, native hidden-layout validation, resume и frozen
  baseline benchmark. Dataset v1 остаётся читаемым.

## Инфраструктура проверки

- Обычные presets используют `BUILD_TESTS=OFF`; для тестов нужен test preset.
- Desktop-конфигурация требует локальный `Qt6_DIR` или `CMAKE_PREFIX_PATH`;
  `windows-headless-tests` собирает solver и CLI без Qt.
- `testCountour2D.cpp` включён в test target и проверяет публичные `Create` и
  `GetPoints`.
- MSVC собирает C++ с `/EHsc`; предупреждение C4530 устранено.
- Qt runtime разворачивается рядом с приложением и тестовым executable для
  соответствующей Debug или Release конфигурации.
- Исходные C++-файлы нормализованы по корневому `.clang-format`.
- В ветке A1 добавлен GitHub Actions workflow для Linux/Windows headless и
  Python 3.13; Qt desktop пока проверяется локально.
- `tools/run_checks.py` предоставляет единые команды `architecture`, `nesting`, `headless`,
  `python`, `desktop` и `all` без установки или очистки зависимостей.
- Solver tests разделены по владельцам GridCore, PolygonCore, Search, Json,
  Learning и compatibility. Локально пройдены clean nesting 44/44, полный
  Windows headless 46/46, MSVC+Qt desktop 47/47 и Python 24/24.
- Общий grid/polygon contract corpus классифицируется C++ parser/validator,
  Python native binding и JSON Schema Draft 2020-12.
- Allow-list автоматически запрещает новые Core → Qt, недопустимые внутренние
  include/import и CMake target edges; legacy-исключения ограничены известными
  файлами и должны быть удалены в A6.
- clang-format и clang-tidy доступны через LLVM из Visual Studio, но ещё не
  подключены как CMake/CI targets.
- A4 сохраняет 44/46/47 CTest entries; локально пройдены clean nesting 44/44,
  полный Windows headless 46/46, MSVC+Qt desktop 47/47 и Python 24/24. Общий
  runtime имеет unit tests с управляемыми часами, а architecture self-tests
  отдельно запрещают Search → Json.
- A5 сохраняет C++ targets и wire-форматы. Python pytest проходит 30/30,
  architecture self-tests — 6/6; минимальные grid/polygon v1 outputs старого и
  нового pipeline совпадают побайтово по семи файлам каждый.
- M6.1R считается готовым к каноническому прогону, но не завершённым: до
  генерации 384 задач manifest SHA-256 и validation benchmark не зафиксированы.
- Локальный Python suite после переноса проходит 43/43; clean nesting — 44/44,
  Windows headless — 46/46, MSVC+Qt desktop — 47/47. Архитектурная проверка и
  шесть отрицательных self-tests также проходят.
- Clean nesting preset не создаёт Math/Domain/Contract/App/Qt targets. После A4
  20 CLI golden projections совпадают с A1/A3 на MinGW и MSVC; Linux/CI требуют
  публикации ветки.

## Ограничения desktop-геометрии

- Фигура — набор квадратных `Cell2D`, а не полигон с кольцами.
- Повороты четверть-оборотами выполняются вокруг целочисленного cell pivot.
- Пересечения определяются через bounding boxes фигур и клеток.
- Контур клетки дискретизирует кривые фиксированным числом точек и не является
  границей целой детали.
- Клеточная GUI-модель не использует полигональные ограничения; отдельная
  полигональная вкладка показывает результат solver-а без ручного редактирования.
- Загрузка сцены принимает только `cell_grid` и сериализованный поворот 0°.

## Ограничения ML

Код обучения, локальный run manifest и ONNX-экспорт добавлены, но M3 ещё нельзя
считать завершённым до PPO и прохождения test quality gate. На Windows RTX 4060
проверены официальный CUDA PyTorch и частичный BC до checkpoint эпохи 35;
незавершённый запуск был штатно остановлен без изменения Git. GUI не подключён
ни к клеточной, ни к полигональной policy; C++ ONNX inference перенесён в M6.
Текущая клеточная модель несовместима с динамическим polygon action space и
не выдаётся за полигональную.
