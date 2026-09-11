# Архитектурный аудит A0

Статус: завершён. Дата проверки: 2026-09-11.

## Контекст и границы снимка

Аудит охватывает C++-ядро, Qt desktop, CLI, pybind11, Python/ML, JSON-контракты,
CMake и тесты. В качестве стабильной точки используется коммит `dc68168`
(`feat: integrate polygon solver into desktop`, M5). Отдельно рассмотрен текущий
рабочий каталог с незавершённым M6.1: 13 изменённых tracked-файлов, четыре новых
файла и более тысячи строк незакоммиченного delta.

M6.1 не считается частью стабильного baseline. Аудит не изменяет production-код
и не предлагает начинать реорганизацию поверх смешанного рабочего дерева.

Проверки текущего рабочего дерева:

| Контур | Результат |
|---|---:|
| Windows headless CTest | 38/38, 106,81 с |
| Windows MSVC + Qt CTest | 38/38, 89,63 с |
| Python 3.13 pytest | 31/31, 56,53 с |

Система функционально стабильна. Найденные проблемы относятся прежде всего к
стоимости следующих изменений, а не к текущей корректности результатов.

## Сильные стороны, которые необходимо сохранить

- Полигональная геометрия и валидатор не зависят от Qt и Python.
- Любое решение повторно проверяется детерминированным C++-валидатором.
- Форматы problem, solution, trajectory и dataset версионированы.
- Baseline используют seed, ограниченные бюджеты и общий порядок сравнения.
- Кривые переводятся в авторитетную целочисленную геометрию до поиска.
- Python не получает возможности напрямую изменять C++-состояние среды.
- Headless, desktop и Python-контуры имеют работающие локальные тесты.

## Фактическая карта компонентов

Объём собственных исходников на момент аудита:

| Область | Файлы | Строки | Наблюдаемая ответственность |
|---|---:|---:|---|
| `src/core/contract` | 20 | 485 | legacy UI events и view-интерфейсы |
| `src/core/domain` | 26 | 1 496 | legacy клеточные Board/Figure |
| `src/core/app` | 58 | 3 834 | legacy команды и polygon workflow |
| `src/solver` | 18 | 4 609 | grid, polygon, IO, search и learning |
| `src/gui` | 16 | 1 808 | Qt legacy и polygon presentation |
| `src/cli` | 1 | 248 | grid/polygon solve и validate |
| `src/python` | 1 | 474 | все pybind11 bindings |
| `python/aipackaging_ml` | 18 | 3 309 | данные, обучение, policy и export |

Фактический граф CMake:

```text
Qt GUI -------> App -------> Domain -> Contract -> AIPackaging_Math
  |              |
  |              +-------> Solver <-------- CLI
  +----------------------> Solver <----- pybind11
```

Стрелка `A -> B` означает «потребитель A зависит от компонента B».
`AIPackaging_App` публично экспортирует Domain, Contract и Solver, а GUI публично
экспортирует App, Contract, Math и Qt. Headless-конфигурация отключает только GUI:
Domain, Contract и App всё равно добавляются безусловно в `src/CMakeLists.txt`.

Внутри `AIPackaging_Solver` границы существуют только по именам файлов:

```text
grid types/environment/search/io/learning
polygon types/environment/search/io/learning
```

Все восемь translation units входят в один target с одной плоской публичной
include-директорией. Clipper2 и nlohmann/json являются private link-зависимостями,
что правильно, но их потребители невозможно собирать и тестировать раздельно.

### Текущие публичные контракты

| Контракт | Фактический владелец | Потребители | Замечание |
|---|---|---|---|
| `Event`, `IView` и UI events | Contract | legacy App и Qt GUI | интерфейс UI, а не domain contract |
| `Board`, `Figure`, `SelectionModel` | Domain | legacy App | клеточная изменяемая модель |
| status, solver config/metrics/metadata | `gridtypes.h` | grid, polygon, App, GUI, Python | общий контракт имеет grid-владельца |
| `GridProblem/State/Action/Solution` | Solver grid | CLI, learning, Python | research API M1-M3 |
| `PolygonProblem/State/Action/Solution` | Solver polygon | CLI, learning, App, Python | целевой product API |
| grid/polygon load/save | Solver IO | CLI, App, bindings | JSON скрыт, filesystem смешан с codec |
| learning observations/reward | Solver learning | pybind11, Python | C++ является источником action space |
| workspace snapshot/actions | App | Qt GUI | содержит concrete solver-типы |
| dataset/training/model contracts | Python package + schemas | Python CLI и experiments | проверяются вручную в нескольких модулях |

В целевой архитектуре каждому из этих контрактов назначен отдельный владелец;
таблица приведена в [целевой архитектуре](target-architecture.md).

## Основные потоки данных и владельцы

### Grid research

```text
grid JSON -> gridio -> GridProblem -> GridEnvironment
                                  -> GridSolver -> GridSolutionValidator
                                  -> GridLearningEnvironment -> pybind11 -> Python
```

`GridEnvironment` владеет проверенной задачей и ориентациями. `GridState` является
value-state. Learning environment дополнительно владеет состоянием эпизода и
постоянным каталогом действий. Этот контур не подключён к desktop и нужен для
воспроизводимости M1-M3.

### Polygon research и desktop

```text
polygon JSON -> polygonio -> PolygonProblem -> PolygonEnvironment
                                            -> PolygonSolver -> validator
                                            -> PolygonLearningEnvironment -> Python
                                            -> PolygonWorkspaceController -> Qt
```

`PolygonEnvironment` одновременно нормализует аналитические пути, создаёт
ориентации, проверяет действия, генерирует кандидатов, вычисляет objective,
растрирует признаки и содержит функции независимой валидации. Desktop-controller
владеет problem, environment, solution, worker-потоком и presentation mapping.

### Legacy desktop

```text
QtView -> Event -> MainController -> state/commands -> PackingController
                                                -> Board <-> Figure
```

Этот контур использует другую геометрию, другую сериализацию, другую валидацию и
другие правила перемещения. Он не может быть основой полигонального продукта без
повторной реализации уже существующего solver-контракта.

## Реестр проблем

Шкала приоритета отражает риск для развития проекта: `high` требуется устранить
до указанного этапа, `medium` допускает контролируемое откладывание, `low` не
оправдывает самостоятельный рефакторинг. Сложность: `S` — локально, `M` — один
модуль, `L` — несколько границ, `XL` — продуктовая миграция.

| ID | Приоритет | Сложность | Проблема | Срок |
|---|---|---:|---|---|
| A0-01 | high | L | Solver является монолитным target | до M6.2 |
| A0-02 | high | M | Общие solver-типы принадлежат grid API | до M6.2 |
| A0-03 | high | M | Application и GUI раскрывают concrete solver API | до M6.3 |
| A0-04 | high | M | Headless/Python сборки тянут legacy desktop-слои | до M6.2 |
| A0-05 | high | M | Нет автоматического контроля архитектурных границ и CI | до M6.2 |
| A0-06 | medium | L | Grid и polygon дублируют search runtime | до расширения baseline |
| A0-07 | medium | M | JSON Schema не является исполняемым источником истины | до нового wire v2 |
| A0-08 | medium | M | Концентрация обязанностей в крупных файлах | вместе с A0-01 |
| A0-09 | medium | M | Поток выполнения находится внутри controller | до ONNX backend |
| A0-10 | medium | L | Legacy владеет объектами и событиями через shared_ptr/RTTI | изолировать, затем удалить |
| A0-11 | medium | M | Один C++ test target скрывает реальные зависимости | вместе с A0-01 |
| A0-12 | medium | S | M6.1 не имеет отдельной стабильной точки | до любого рефакторинга |
| A0-13 | low | L | Legacy использует global namespace и плоские includes | не исправлять отдельно |

### Состояние после страховки A1

Ветка `codex/a1-architecture-safety` закрывает отсутствие автоматической
диагностики из A0-05 и разделяет единый test executable из A0-11 без изменения
production-кода. Текущие нарушения App/GUI → Solver не объявлены нормой: они
зафиксированы точечными исключениями с `removeBy: A6`. Монолит Solver, владельцы
общих типов и другие замечания A0 остаются задачами A2–A8.

Критических дефектов, требующих аварийной остановки разработки, не найдено.

### A0-01. Монолитный `AIPackaging_Solver`

Доказательство: `src/solver/CMakeLists.txt` включает grid/polygon environment,
search, IO и learning в один target. `polygonenvironment.cpp` содержит 868 строк:
аппроксимацию кривых, topology, orientation, collision, candidates, raster,
objective и validator. Любое изменение геометрии пересобирает и затрагивает
адаптеры данных и обучения.

Последствие: M6.2 не имеет устойчивой границы между геометрией, поиском и
observation; будущий ONNX backend рискует зависеть от лишнего IO/learning-кода.

Решение: разделить target на Core, Search, Learning и Json, сохранив текущие
публичные функции через временный compatibility target.

### A0-02. Полигональный контракт зависит от клеточного

Доказательство: `polygontypes.h:9` включает `gridtypes.h`, а `PolygonProblem`
использует `GridObjectiveDefinition`; `PolygonSolution` использует объявленные
там `SolveStatus`, `SolverMetrics` и `SolverMetadata`.

Последствие: grid нельзя архивировать независимо, а нейтральные типы имеют
ложного владельца. Изменение grid wire contract способно затронуть polygon ABI.

Решение: выделить `ObjectiveDefinition`, `SolveStatus`, `SolverMetrics`,
`SolverMetadata` и `ValidationResult` в общий contract, а общие параметры и
управление поиском — в Search contract.

### A0-03. Утечка solver API в application/presentation

Доказательство: `polygonworkspaceview.h` включает `gridtypes.h`,
`polygonsolver.h` и `polygontypes.h`; GUI принимает `SolverConfig`, а snapshot
содержит `PolygonSolverProgress`, `PolygonObjectiveComponents` и `SolverMetrics`.
`PolygonWorkspaceController` хранит concrete `PolygonEnvironment`.

Последствие: добавление ONNX/hybrid backend или изменение solver API требует
пересборки и правок application и Qt. Заявленная граница «GUI получает только
presentation model» соблюдается частично.

Решение: в application определить собственные `NestingRunRequest`,
`NestingProgress`, `NestingResultSummary` и `PolygonSceneView`; преобразование к
solver-типам выполнять в infrastructure adapter.

### A0-04. Сборочные флаги не изолируют продуктовые контуры

Доказательство: `src/CMakeLists.txt:2-5` безусловно добавляет Domain, Contract,
App и Solver. `AIPackaging_App` публично связывает все три зависимости, а его
include paths экспортируют внутренние `services/packing` и `services/io`.

Последствие: solver-only CLI и Python wheel зависят от конфигурации legacy-кода;
нарушения границ маскируются транзитивными includes и libraries.

Решение: собирать Core/Search/Json независимо; legacy App/Domain создавать только
для desktop или legacy tests; заменить широкие PUBLIC связи на PRIVATE там, где
тип не встречается в публичном заголовке.

### A0-05. Границы не проверяются автоматически

Доказательство: репозиторий GitHub не содержит CI. Нет теста запрещённых include
или link edges. Успех зависит от локальных Windows-команд и установленного Qt.

Последствие: циклическая зависимость или случайный Qt/Python include в ядре будет
обнаружен поздно. Воспроизводимый датасет не защищён обязательной проверкой кода.

Решение: добавить CMake dependency tests и GitHub Actions для Linux headless,
Python и Windows MSVC; desktop Qt job сделать отдельным после определения способа
установки Qt в runner.

### A0-06. Дублирование search orchestration

Доказательство: `gridsolver.cpp` и `polygonsolver.cpp` отдельно реализуют timeout,
metrics, ordering, random permutations, beam truncation, metadata и сравнение
partial/full. Геометрическая часть различается, жизненный цикл поиска — нет.

Последствие: cancellation и progress добавлены только polygon path; исправления
бюджетов и статусов приходится синхронизировать вручную.

Решение: общий внутренний `SearchRuntime` и параметризованные алгоритмические
циклы. Candidate ranking и state evaluation остаются geometry-specific.

### A0-07. Три реализации контракта данных

Доказательство: JSON Schema хранится в `schemas/`, C++ вручную перечисляет поля в
`gridio.cpp` и `polygonio.cpp`, Python отдельно реализует `_require_keys`, hashes,
canonical JSON и manifest validation. Runtime-зависимости от JSON Schema нет.

Последствие: схема, C++ parser и Python verifier могут расходиться при следующей
версии. Существующие тесты проверяют примеры, но не полную parity матрицу.

Решение: wire parser остаётся ручным и типобезопасным, но canonical schema cases
генерируются один раз и прогоняются одинаково через C++ и Python. Общие Python
canonical/hash/gzip primitives должны иметь одного владельца.

### A0-08. Файлы с несколькими причинами для изменения

Доказательство: `polygonenvironment.cpp` — 868 строк,
`polygon_dataset.py` — 896, `packingcontroller.cpp` — 551, `qtview.cpp` — 534,
`polygonio.cpp` — 530, `bindings.cpp` — 510. `polygon_dataset.py` одновременно
описывает shapes, generation, family hashing, cache, shards, replay и verification.

Последствие: ревью и локальные тесты сложнее, конфликты между следующими этапами
вероятнее. Сам размер не является дефектом; проблема подтверждается количеством
независимых обязанностей.

Решение: декомпозировать по уже выбранным границам, не вводя библиотеку на каждый
файл.

### A0-09. Controller совмещает use case и execution infrastructure

Доказательство: `PolygonWorkspaceController` владеет `std::jthread`, stop token,
throttling progress, runId, UI dispatch, persistence, validation и presentation
mapping.

Последствие: второй backend увеличит ветвление controller; thread policy сложно
переиспользовать в CLI и тестировать независимо от view callback queue.

Решение: controller оставить синхронной state machine, а выполнение передать
`INestingJobRunner`. Runner владеет потоком, отменой и throttling; controller
принимает типизированные progress/completion events с generation id.

### A0-10. Legacy lifetime и event dispatch неочевидны

Доказательство: `Board::add` сохраняет `Figure` и передаёт фигуре
`shared_from_this()` как listener (`board.cpp:41-48`); `Figure` хранит listeners
как `shared_ptr` (`algs.h:50-62`). Аналогично QtView сильно хранит EventListener,
а MainController хранит view. Event subclasses разбираются switch +
`static_cast`, а отдельные команды — через `dynamic_cast`.

Последствие: Board/Figure образуют цикл владения до явного `remove`, lifetime
опирается на порядок разрушения, расширение событий не проверяется типами.

Решение: не переписывать этот механизм как самостоятельную инвестицию. Поместить
legacy targets за флаг, запретить новые зависимости и удалить после достижения
полигональным workspace необходимого функционального паритета.

### A0-11. Тестовый target скрывает модульность

Доказательство: `tests/CMakeLists.txt` собирает domain, app, solver, math и GUI
тесты в один `AIPackaging_Tests`, который публично линкует почти все библиотеки.

Последствие: тест может использовать транзитивно доступный внутренний API;
изолированная собираемость Core или Json не проверяется.

Решение: разделить тестовые executables по production targets и оставить CLI
end-to-end отдельным уровнем.

### A0-12. Незавершённый M6.1 пересекает критические границы

Доказательство: текущий worktree изменяет version, docs, CLI Python, bindings,
dataset/verifier и schemas; каноническая генерация остановлена до manifest hash.

Последствие: механическое перемещение файлов смешает функциональный M6.1 delta с
архитектурным и усложнит проверку побайтовой воспроизводимости.

Решение: до A1 либо закончить M6.1, либо перенести весь delta в отдельный
worktree/branch. Канонические artifacts могут генерироваться параллельно, так как
они игнорируются Git.

## Итоговая рекомендация

Перед M6.2 требуется отдельный архитектурный этап. Он должен стабилизировать
общие contracts, разделить polygon Core/Search/Learning/Json, обеспечить
минимальную headless сборку и декомпозировать Python dataset path. Это уменьшит
риск обучить и заморозить полигональную модель поверх временного API.

Application ports и вынос runner обязательны до M6.3, когда появится ONNX backend.
Полный вывод legacy GUI выполняется после появления полигонального редактора или
явного решения, что редактирование не требуется. Детальный порядок зафиксирован
в [плане переработки](architecture-refactoring-plan.md).
