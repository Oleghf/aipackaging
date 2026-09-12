# Архитектура

## Текущая система

```text
Qt GUI
  -> контроллеры, команды и стратегии приложения
    -> клеточная доменная модель и валидация
      -> геометрические примитивы AIPackaging_Math
```

`FirstFitAutomaticPlacementStrategy` перебирает позиции в порядке строк.
`AutomaticPackPlanner` строит раскладку на временной доске и применяет только
полный план. Граница стратегии пригодна для повторного использования, но
клеточная `Figure` не является целевой полигональной моделью.

Параллельно реализован независимый от Qt модульный контур раскроя:

```text
JSON -> GridCore / PolygonCore -> Search -> independently validated solution
                         |
                      Learning -> pybind11
```

`AIPackaging_NestingCore` владеет общими objective/status/metrics/metadata и
результатом валидации. `GridCore` и `PolygonCore` владеют состояниями,
допустимостью, кандидатами и objective; `Search` — baseline, `Json` —
wire-адаптерами, а `Learning` — пошаговыми средами. `SearchContracts` отделяет
конфигурацию, progress и execution-control алгоритма от его реализации.
`AIPackaging_SolverImpl` и старое имя
`AIPackaging_Solver` остаются только INTERFACE compatibility targets до миграции
App/GUI в A6. Контур не использует `Board/Figure`. CLI сохраняет полный либо
лучший частичный результат вместе с конфигурацией и метриками.

M4 добавляет второй, также независимый от Qt контур:

```text
polygon_problem v1 -> curve flattening -> PolygonEnvironment -> baseline
                                                |                 |
                                        dynamic NFP actions  polygon_solution v1
                                                |
                                      PolygonSolutionValidator
```

Клеточный и полигональный контракты сосуществуют; canonical C++-заголовки
доступны как `aipackaging/nesting/...`, а старые плоские заголовки являются
forwarding API. Канонический `polygon_types.h` больше не включает grid API.
Формат определяется корневым
полем `format`, поэтому прежние задачи, CLI-команды и датасеты не мигрируют.

## Целевая система

```text
Импорт -> нормализованная геометрия -> генератор кандидатов
                                           |
                                  маска допустимых действий
                                           |
кодировщик состояния -> нейросеть -> выбранное размещение
                                           |
                                    точный валидатор
                                           |
                              раскладка, метрики, экспорт
```

### Геометрическое ядро

Оно нормализует масштаб и ориентацию, аппроксимирует кривые, представляет внешние
кольца и отверстия, строит offsets зазора, формирует кандидатов, точно проверяет
границы и пересечения и вычисляет метрики. Авторитетное представление должно
использовать масштабированные целочисленные координаты полигонов. Масштаб и
допуск фиксируются при импорте. В M4 вход задаётся в миллиметрах, а все
авторитетные операции выполняются в `int64` микронах через Clipper2. Отрезки,
дуги и cubic Bézier превращаются в кольца с контролируемой ошибкой.

### Среда решателя

Среда хранит неизменную задачу и изменяемое состояние раскладки. Шаг принимает
идентификатор кандидата, проверяет и применяет его, затем возвращает состояние,
компоненты reward и признак завершения. Один контракт обязателен для нейросети и
эвристик.

В клеточной реализации `GridEnvironment` предоставляет value-state занятости,
уникальные четверть-оборотные ориентации, стабильный набор кандидатов, применение
действий и вычисление objective. `GridLearningEnvironment` строит поверх него
постоянный каталог действий, динамическую mask и reward, не дублируя геометрию.

```text
Python GridNestingEnv -> pybind11 -> GridLearningEnvironment -> GridEnvironment
       |                                      |
 dataset generator/replay              objective + validator
```

NumPy-наблюдения владеют копией снимка и доступны только для чтения. Поэтому
Python-код не может изменить C++ value-state через общий буфер. Датасет не
хранит объёмные observations: `grid_trajectory` сохраняет индексы, дублирующие
действия, reward-аудит и итоговый `grid_solution`, а loader воспроизводит
наблюдения повторным проигрыванием.

Для rollout M3 среда отдельно возвращает статические признаки и компактную
динамическую часть. Каталог, признаки кандидатов и маски фигур копируются в
Python один раз на эпизод; `stepCompact` обновляет только occupancy, remaining,
action mask и objective.

После A5 Python dataset pipeline разделён по владельцам:

```text
datasets.serialization/cache
          |
    +-----+------------------+
    |                        |
datasets.grid            datasets.polygon
generation/rollout       recipes/generation/rollout
pipeline/verification    pipeline/replay/verification
    |                        |
dataset.py facade        polygon_dataset.py facade
          \                 /
           C++ binding + Learning environments
```

Общая инфраструктура не импортирует native binding, environment или ML. Grid и
polygon verification не вычисляют геометрию самостоятельно: решения и действия
проверяются через C++ validator, comparator и action space. Старые Python import
paths сохранены фасадами. Dataset CLI отделён от ленивых ML handlers и работает
без train-extra.

### Генератор кандидатов

В MVP сеть не ищет неограниченные вещественные координаты. Генератор возвращает
ограниченный детерминированный набор допустимых `(деталь, поворот, позиция)`.
Для клеток каталог постоянен. Для полигонов он динамический: pairwise no-fit
polygons вычитаются из inner-fit rectangle текущей ориентации, а вершины
оставшейся области становятся кандидатами текущего шага. Любой кандидат снова
проходит независимую точную проверку.

`PolygonLearningEnvironment` возвращает базовые occupied/clearance raster и
условное observation выбранной пары: четыре канала 128×128, динамические actions
и признаки кандидатов. Индексы таких действий нельзя сохранять между шагами.

### Нейросетевая политика

Начальная политика иерархическая: кодирует занятость листа и оставшиеся детали,
выбирает деталь и поворот, оценивает допустимые позиции и выбирает действие.
Обучение выполняется в Python, deployment планируется через версионированный ONNX
без зависимости desktop-приложения от Python.

```text
occupancy + parts + objective -> encoder -> instance -> rotation
                                      |                    |
                                    critic        legal positions only
                                                           |
                                                   placement head
```

Маски каждого уровня выводятся из C++ `actionMask`, а не предсказываются сетью.
Для deployment encoder и placement head экспортируются отдельными ONNX-графами.
Neural/hybrid provenance записывается в `grid_solution` v2; baseline и датасет
M2 сохраняют совместимый формат v1.

### Desktop-приложение

Qt отвечает за редактирование, визуализацию и проверку пользователем. GUI зависит
от прикладного интерфейса решателя и не содержит collision или ML-логику.

M5 сохраняет клеточный экран и добавляет отдельный read-only полигональный
workspace:

```text
PolygonWorkspaceWidget
  -> PolygonWorkspaceController
    -> IPolygonSolverBackend
      -> PolygonEnvironment + PolygonSolutionValidator
```

Контроллер владеет `std::jthread`; progress и completion возвращаются в главный
Qt-поток через queued dispatcher. Только независимо проверенный неотменённый
результат разрешается сохранять. Presentation-модель уже содержит перенесённые
кольца в миллиметрах, поэтому Qt выполняет только отрисовку.

## Предлагаемый доменный контракт

```text
ProblemInstance: units, sheet, parts[], constraints, objective
Part: id, outerRing, holes[], quantity, allowedRotations, allowMirror
Placement: partInstanceId, translation, rotation, mirrored
Solution: placements[], feasibility, objectiveComponents, solverMetadata
```

Для MVP `objective` задаёт уплотнение к левому краю и остаток у правого края.
Он хранит `usedLength`, `primaryRemnantWidth`, площадь крупнейшего дополнительного
прямоугольника и штраф фрагментации. Простого поля `freeArea` недостаточно.

Сериализуемые контракты имеют версию схемы. Метаданные модели фиксируют
совместимую схему, параметры геометрии, нормализацию признаков и версию action
space.

## Обязательные инварианты

- Экспортируемые детали не пересекаются и не выходят за полезную область листа.
- `sheetMargin` и `partSpacing` одинаково трактуются генератором, валидатором,
  метриками и экспортом; `kerf` пока является только метаданными toolpath.
- Модель не может обходить или переопределять валидатор.
- Результат оценки воспроизводим при одинаковых входе, версиях, настройках и seed.
- Нейросеть и baseline используют один валидатор и одну целевую функцию.
- Допуски аппроксимации входят во входные данные или метаданные запуска.

Направление зависимостей: `gui -> app -> domain`; адаптеры решателей зависят от
контракта решателя и домена, но домен не зависит от Qt Widgets, PyTorch, ONNX
Runtime или конкретного алгоритма.

Фактический модульный build graph после A3:

```text
AIPackaging_NestingCore
  <- SearchContracts
  <- GridCore
  <- PolygonCore <- Clipper2 (PRIVATE)

Search   <- SearchContracts + GridCore + PolygonCore
Json     <- SearchContracts + GridCore + PolygonCore + nlohmann/json (PRIVATE)
Learning <- GridCore + PolygonCore

SolverImpl (INTERFACE) <- Search + Json + Learning
Solver (INTERFACE) <- SolverImpl <- legacy App / GUI
```

`NestingCore`, `GridCore`, `SearchContracts` и `Learning` не линкуют Qt, Math,
JSON или Python. Clipper2 доступен только реализации polygon candidate generation,
а nlohmann/json — только `Json`. CLI напрямую использует `Search` и `Json`,
pybind11-модуль — `Search`, `Json` и `Learning`. При `BUILD_DESKTOP=OFF` и
`BUILD_LEGACY_TESTS=OFF` CMake не создаёт Math, Domain, Contract и App targets.

Внутри `Search` после A4 общий runtime управляет `steady_clock`, cooperative
cancellation, progress, счётчиками, metadata и lifecycle ordered/random/beam.
Grid/polygon adapters сохраняют собственные state/action, генерацию кандидатов,
left-bottom/NFP ranking, objective и post-validation:

```text
SearchExecutionControl
          |
     SearchRuntime
      /        \
 Grid adapter  Polygon adapter
      |             |
   GridCore      PolygonCore
```

Runtime синхронный и не владеет потоками. Прежние polygon execution-типы
являются совместимыми aliases, а `runGridProblem` предоставляет тот же контракт
grid-потребителям.
