# Текущее состояние

Проверено 2026-09-10.

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
- CI отсутствует.
- clang-format и clang-tidy доступны через LLVM из Visual Studio, но ещё не
  подключены как CMake/CI targets.

## Ограничения геометрии

- Фигура — набор квадратных `Cell2D`, а не полигон с кольцами.
- Повороты четверть-оборотами выполняются вокруг целочисленного cell pivot.
- Пересечения определяются через bounding boxes фигур и клеток.
- Контур клетки дискретизирует кривые фиксированным числом точек и не является
  границей целой детали.
- Нет модели отверстий, дефектов, единиц, kerf, зазора и допуска кривых.
- Загрузка сцены принимает только `cell_grid` и сериализованный поворот 0°.

## Ограничения ML

Код обучения, локальный run manifest и ONNX-экспорт добавлены, но M3 ещё нельзя
считать завершённым до канонического CUDA-запуска и прохождения test quality
gate. Windows CPU-smoke и Python bindings проходят; включение эталонной WSL2
среды требует административного PowerShell и перезагрузки. Smart App Control не
отключается и на этом хосте блокирует запуск свежесобранного неподписанного MSVC
CLI; тот же CLI и CTest проходят в headless MinGW-сборке. GUI не подключён к
новой policy; C++ ONNX inference входит в M5.
