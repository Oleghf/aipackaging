# План архитектурной переработки A1-A8

## Правила этапа

- Рефакторинг не меняет геометрическое поведение и JSON wire formats.
- Каждый этап завершается зелёными headless, desktop и Python regression tests.
- Перемещение и изменение поведения не объединяются в одном коммите.
- Compatibility headers/targets удаляются только после миграции потребителей.
- Производительность polygon fixtures фиксируется до A1 и сравнивается после
  каждого изменения; допустимо только объяснённое отклонение.

## Предусловие A1

Текущий M6.1 delta должен быть либо завершён и зафиксирован отдельным коммитом,
либо целиком перенесён в отдельный worktree/branch. Смешивать его с CMake и
source moves запрещено. Каноническую генерацию можно продолжать независимо в
`artifacts/`.

Зафиксировать baseline:

- 38 headless CTest;
- 38 desktop CTest;
- 31 Python pytest;
- solutions всех polygon CLI fixtures;
- deterministic smoke dataset bytes;
- длительность пяти baseline как диагностический benchmark.

## A1. Архитектурная страховка

Сложность: `S`. Обязателен до остальных этапов.

- Добавить локальные `check-headless`, `check-python`, `check-desktop` presets или
  scripts без привязки к пользовательскому пути Qt.
- Добавить dependency check с allow-list допустимых target/include edges.
- Разделить текущий `AllTests` минимум на Solver и LegacyApp, не меняя тестовый код.
- Добавить contract corpus с валидными и невалидными grid/polygon JSON, который
  прогоняется C++ parser, Python verifier и JSON Schema validator.
- Создать GitHub Actions Linux headless + Python и Windows MSVC headless.

Готово, когда нарушение Core → Qt/App либо Application → concrete solver
обнаруживается автоматически, а локальные и CI-команды дают одинаковый набор
тестов. Откат — удаление только новых проверочных targets/workflows.

Статус реализации: подготовлено в ветке `codex/a1-architecture-safety`.
Добавлены стандартный `tools/run_checks.py`, CMake/include/import allow-list,
отрицательные self-tests, четыре изолированных C++ test targets, общий contract
corpus и GitHub Actions. Локально пройдены 40/40 headless и 41/41 desktop CTest.
Этап отмечается полностью завершённым после зелёного локального desktop-прогона
и первого успешного CI run; до этого ветка не сливается.

## A2. Общие contracts и честный build graph

Сложность: `M`. Обязателен до M6.2.

- Создать общий Core-header для objective, status, metrics/metadata и validation
  result, а Search-header — для solver kind/config/control; убрать зависимость
  polygon types от `gridtypes.h`.
- Ввести include-prefix `aipackaging/nesting`; оставить старые headers forwarding.
- Создать `AIPackaging_NestingCore` и временный `AIPackaging_Solver` INTERFACE
  compatibility target.
- Сделать legacy Domain/App условными для desktop/legacy tests.
- Исправить PUBLIC/PRIVATE зависимости по фактическим публичным signatures.

Готово, когда polygon Core и CLI конфигурируются в чистой директории без Qt,
AIPackaging_Math и legacy App; сериализованные решения побайтово эквивалентны.
Откат — переключение compatibility target на прежний source list.

Статус реализации: подготовлено в ветке `codex/a2-common-contracts`.
`AIPackaging_NestingCore` владеет нейтральными контрактами,
`AIPackaging_SolverImpl` — прежними восемью translation units, а
`AIPackaging_Solver` сохраняет старые include/link entry points. Локально
пройдены clean nesting 39/39, полный Windows headless 41/41, MSVC+Qt desktop
42/42 и Python 24/24; Linux/CI считаются непроверенными до публикации ветки.

## A3. Разделение polygon Core, Json и Learning

Сложность: `L`. Обязателен до M6.2.

- Разделить `polygonenvironment.cpp` на normalization/topology, collision и
  clearance, candidate generation, objective/raster и validation.
- Выделить `AIPackaging_Json`; nlohmann/json остаётся private.
- Выделить `AIPackaging_Learning`; он не линкует Search и Json.
- Разделить pybind11 bindings на common, grid и polygon translation units без
  изменения Python API.
- Разделить тесты по новым production targets.

Готово, когда public API, action order, objective, exceptions и NumPy shapes не
изменились; forbidden dependency tests проходят. Откат выполняется target за
target благодаря сохранённым forwarding headers.

Статус реализации: подготовлено в ветке `codex/a3-solver-modules`.
Вместо одного `SolverImpl` созданы SearchContracts, GridCore, PolygonCore,
Search, Json и Learning; compatibility targets сохранены. Polygon environment и
pybind11 bindings физически разделены по ответственности. Локально пройдены
clean nesting 44/44, полный Windows headless 46/46, MSVC+Qt desktop 47/47,
Python 24/24 и semantic golden для 20 CLI-решений на MinGW/MSVC. Linux/CI
считаются непроверенными до публикации ветки.

## A4. Общий search runtime

Сложность: `M-L`. Желателен до M6.2 и обязателен до добавления новых baseline.

- Выделить общие timeout, cancellation, progress, metrics и metadata.
- Параметризовать input-order, random permutation и beam lifecycle адаптером
  конкретной среды.
- Оставить candidate enumeration, left-bottom ranking, state и objective в
  grid/polygon implementation.
- Дать grid solver тот же execution-control контракт, не меняя default поведения.

Готово, когда placements и детерминированные counters всех fixtures совпадают с
baseline; cancellation/progress проверяются для обоих контуров. Если generic
слой требует geometry-specific ветвлений, откатить только обобщение algorithms,
сохранив общий runtime счётчиков и отмены.

Статус реализации: подготовлено в ветке `codex/a4-search-runtime`.
`SearchContracts` владеет общими progress/control типами, а закрытый runtime
объединяет ordering, seeded random, beam, timeout, cancellation, метрики и
metadata. Grid/polygon adapters сохраняют domain-specific выбор кандидатов,
objective и валидацию. Локально пройдены clean nesting 44/44, полный Windows
headless 46/46, MSVC+Qt desktop 47/47, Python 24/24 и semantic golden для 20
CLI-решений; Linux/CI требуют публикации ветки.

## A5. Декомпозиция Python data pipeline

Сложность: `M`. Обязателен до production M6.2 training.

- Выделить общие canonical JSON, SHA-256, deterministic gzip и shard IO.
- Разделить polygon shapes/recipes, generation, cache, replay и verification.
- Оформить CLI-команды отдельными handlers; `__main__` оставляет только parser и
  dispatch.
- Исключить импорт PyTorch из dataset-only paths и сохранить базовую установку
  без train extra.
- Проверить byte parity 1 worker/multiple workers/resume на frozen fixture.

Готово, когда smoke dataset и benchmark совпадают побайтово, а полный replay
проходит через неизменный C++ action space. Откат — возврат Python imports к
старым фасадным функциям; формат cache и shards не меняется.

## A6. Application ports и execution adapter

Сложность: `L`. Обязателен до M6.3 desktop inference.

- Заменить solver-типы в `IPolygonWorkspaceView` на application DTO.
- Ввести `IPolygonDocumentGateway`, `INestingJobRunner` и
  `IPolygonWorkspaceOutput`.
- Сделать controller однопоточной state machine; перенести jthread, stop token,
  throttling и backend invocation в infrastructure runner.
- Реализовать baseline adapter и composition root; сохранить M5 поведение.
- Подготовить контракт neural/hybrid backend с обязательным exact validator и
  явным deterministic fallback.

Готово, когда Application tests работают с fake ports без Solver, Qt и
filesystem; Qt tests подтверждают queued delivery, stale generation filtering и
cancel semantics. Откат — временный adapter прежнего controller API.

## A7. Полигональный ONNX backend

Сложность: `L`. Выполняется в M6.3 после quality gate M6.2.

- Реализовать ONNX Runtime adapter за `INestingJobRunner`/backend port.
- Проверять metadata и hashes до запуска.
- Разрешать выбор только действий текущего C++ dynamic catalogue.
- Повторно валидировать результат Core и сравнивать hybrid через общий comparator.
- При несовместимой модели или invalid output применять явно показанный baseline
  fallback, не скрывая provenance.

Готово при parity Python/ONNX на validation, отсутствии невалидных решений и
полном GUI/CLI provenance. Откат — отключение neural backend feature flag.

## A8. Вывод legacy desktop

Сложность: `XL`, но работа дробится по пользовательским сценариям.

- Ввести `BUILD_LEGACY_DESKTOP`, по умолчанию включённый на переходе.
- Запретить новые ссылки из polygon/application модулей на Board/Figure/events.
- Определить необходимый паритет: импорт/создание деталей, просмотр, запуск,
  ручная корректировка и сохранение либо документированный отказ от части функций.
- Переносить по одному use case с acceptance test.
- После паритета отключить legacy по умолчанию, затем удалить Board/Figure,
  универсальные events, старые commands и клеточную Qt-вкладку.
- Grid research и frozen M1-M3 воспроизводимость сохраняются независимо от GUI.

Готово, когда ни один поддерживаемый desktop use case не использует legacy
targets, старые scene-файлы имеют importer либо официально завершённую поддержку,
а clean desktop build проходит без AIPackaging_Math/Domain/Contract. До этого
откатом служит feature flag.

## Порядок относительно roadmap

```text
M6.1 checkpoint
  -> A1 -> A2 -> A3 -> A4 -> A5
  -> M6.2 polygon BC/PPO
  -> A6
  -> M6.3 ONNX deployment
  -> A7
  -> A8 по мере достижения polygon desktop parity
```

A1-A5 образуют отдельный архитектурный этап и не должны смешиваться с обучением.
A6 можно проектировать заранее, но production migration выполняется после
стабилизации Core/Search API. A8 не блокирует исследовательский quality gate.
