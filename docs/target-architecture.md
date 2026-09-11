# Целевая архитектура AIPackaging

## Назначение

Целевая система остаётся модульным монолитом C++ с Python-инструментами обучения.
Она не делится на сервисы и не вводит runtime IPC. Границы нужны для независимой
сборки, тестирования и замены policy backend, а не ради количества библиотек.

Главная продуктовая цепочка — полигональная. Grid solver сохраняется как
изолированный research compatibility-контур для воспроизводимости M1-M3.
Клеточный desktop `Board/Figure` считается legacy и выводится после достижения
полигонального функционального паритета.

## Принципы

1. Геометрия определяет допустимость; policy только выбирает допустимое действие.
2. Wire contract, domain type и presentation model имеют разных владельцев.
3. Core не знает о JSON, Qt, pybind11, ONNX Runtime, filesystem и потоках.
4. Search не выполняет сериализацию и не публикует UI callbacks.
5. Application зависит от портов, а не от конкретного baseline или ONNX backend.
6. Старые заголовки сохраняются forwarding adapters на время миграции.
7. Новые границы подтверждаются CMake targets и dependency tests.

## Модули

### `AIPackaging_NestingCore`

Владеет нейтральными status/metadata/validation-контрактами, аналитической и
нормализованной полигональной геометрией, `PolygonEnvironment`, candidate
generation, objective/comparator и точным validator. Clipper2 является private
implementation dependency.

Core не владеет wall-clock временем, progress, JSON и ML tensors. Публичные
заголовки размещаются под include-prefix `aipackaging/nesting/...`.

### `AIPackaging_Search`

Владеет `SolverKind`, `SolverConfig`, `SearchRuntime`, бюджетами,
timeout/cancellation, progress snapshot, ordering policies, random и beam
orchestration. Зависит только от NestingCore.

Geometry-specific операции предоставляются адаптером среды: enumerate, canApply,
apply, evaluate и stable tie-break. Они не прячутся за общим объектным интерфейсом,
если достаточно compile-time adapter: это сохраняет типобезопасность и не
усложняет горячий цикл виртуальными вызовами.

### `AIPackaging_Learning`

Владеет `PolygonLearningEnvironment`, raster/feature observations, reward и
replay API. Зависит от NestingCore, но не от Search: baseline trajectories могут
проигрываться без подключения алгоритмов поиска.

### `AIPackaging_Json`

Владеет strict problem/solution serializers всех поддерживаемых wire versions.
Зависит от публичных контрактов Core и скрывает nlohmann/json. Filesystem helpers
находятся здесь либо в конкретном CLI/desktop adapter, но не в Core.

### `AIPackaging_Application`

Владеет use cases полигонального workspace, состояниями workflow, application
DTO и портами:

```cpp
struct NestingRunRequest;
struct NestingProgress;
struct NestingRunResult;
class IPolygonDocumentGateway;
class INestingJobRunner;
class IPolygonWorkspaceOutput;
```

DTO содержат только необходимые UI поля. `INestingJobRunner` различает complete,
partial, cancelled и failed. Controller не хранит `PolygonEnvironment`, не
управляет `std::jthread` и не принимает solver structs в публичном view API.

### Infrastructure и adapters

- `BaselinePolygonBackend` связывает Application с Search и validator.
- `OnnxPolygonBackend` в M6.3 реализует тот же application port и всегда выполняет
  post-validation через Core.
- `LocalPolygonDocumentGateway` связывает Application с Json и filesystem.
- `StdThreadNestingJobRunner` владеет `std::jthread`, stop source, throttling и
  generation id; Qt предоставляет только UI dispatcher.
- Qt GUI зависит от Application и отображает presentation DTO.
- CLI зависит от Json, Search и Core validator.
- pybind11 зависит от Core, Learning, Search и Json, но bindings разделены по
  grid/polygon/common translation units.

## Граф допустимых зависимостей

```text
                         +----------------------+
                         | AIPackaging_Json      |
                         +----------+-----------+
                                    |
                                    v
+------------------+      +----------------------+      +------------------+
| Learning         |----->| NestingCore          |<-----| Search           |
+---------+--------+      +----------------------+      +---------+--------+
          ^                                                        ^
          |                                                        |
     pybind11                                                infrastructure
          |                                                        |
          +---------------- Python                       Application ports
                                                                   ^
                                                    +--------------+------+
                                                    |                     |
                                                  Qt GUI            composition root
```

Запрещены зависимости Core → Search/Json/Learning/Application, Application →
concrete Search/Json/Clipper2 и Qt GUI → Core/Search concrete types.

## Владение публичными типами

| Контракт | Владелец | Потребители |
|---|---|---|
| `ObjectiveDefinition`, status, metrics, metadata, validation | NestingCore common | Search, Json, adapters |
| `PolygonProblem`, geometry, placement, solution | NestingCore polygon | Search, Learning, Json |
| solver kind/config, `SearchControl`, progress, execution result | Search | CLI, infrastructure |
| observation/reward/replay | Learning | pybind11, Python |
| wire format v1/v2 | Json + schemas | CLI, desktop gateway, Python audit |
| workspace request/progress/result/view | Application | Qt, infrastructure |
| NumPy/Python wrapper types | Python package | training and datasets |

Внешний wire JSON остаётся совместимым. Перемещение C++ declarations между
заголовками не является причиной повышать wire version.

## Потоки выполнения

### Baseline CLI

```text
file -> Json parser -> PolygonProblem -> Search -> Core validator
     <- Json writer <- PolygonSolution <---------+
```

### Обучение

```text
dataset JSONL -> Python verifier -> pybind Json/Core/Learning -> observation
policy action -> dynamic catalogue -> Core apply/validator -> trajectory audit
```

Python отвечает за orchestration, multiprocessing, datasets и tensors. C++
остаётся источником истины для геометрии, действий, objective и validation.

### Desktop baseline и ONNX

```text
Qt action -> Application controller -> INestingJobRunner
                                      -> selected backend
                                      -> exact Core validator
completion -> Application DTO -> queued Qt presentation
```

Controller меняет состояние только в UI-потоке. Runner никогда не вызывает GUI
напрямую. Каждый event содержит generation id; устаревшие events игнорируются.
Отменённый partial отображается, но не становится сохраняемым решением.

## Совместимость и переходные контуры

- `grid_problem`/`grid_solution` и M1-M3 dataset остаются читаемыми.
- Grid environment/search/learning перемещаются в временный
  `AIPackaging_GridResearch`; новые product-функции на него не ссылаются.
- Старый `AIPackaging_Solver` на переходе является INTERFACE compatibility target,
  агрегирующим новые библиотеки.
- Старые плоские headers становятся forwarding headers с deprecation comment;
  внутренний код использует namespaced include-prefix.
- `Board/Figure` и универсальные UI events остаются в
  `AIPackaging_LegacyDesktop`, собираемом только с `BUILD_LEGACY_DESKTOP=ON`.
- Удаление legacy допускается только после миграции нужных пользовательских
  сценариев и отдельного решения о старом scene format.

## Тестовая архитектура

Production target имеет соответствующий test executable: Core, Search, Json,
Learning, Application, Qt adapters и Legacy. End-to-end CLI/Python tests остаются
отдельными. Минимальные compile/link tests подтверждают, что:

- Core собирается без Qt, nlohmann public headers, Python и App;
- Learning собирается без Search и filesystem;
- Application собирается с fake ports без Solver/Clipper2/Qt;
- Qt headers не встречаются вне GUI adapter;
- запрещённые include edges приводят к ошибке проверки.

GitHub Actions выполняет Linux headless и Python jobs на каждый push/PR, Windows
MSVC — для C++ parity. Qt desktop job добавляется отдельно и не блокирует первые
архитектурные изменения, пока способ установки Qt на runner не закреплён.

## Что намеренно не делается

- Микросервисы, отдельные процессы solver и сетевые API.
- Общий абстрактный geometry interface для grid и polygon любой ценой.
- Переписывание рабочего Clipper2-кода одновременно с перемещением модулей.
- Исправление всех имён и форматирования legacy перед его удалением.
- Изменение problem/solution/dataset wire formats без отдельного ADR.
