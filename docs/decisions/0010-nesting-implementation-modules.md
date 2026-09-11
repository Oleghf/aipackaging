# ADR-0010: границы реализации nesting-модулей

- Статус: принято
- Дата: 2026-09-11

## Контекст

После A2 общие контракты имели отдельного владельца, однако восемь translation
units геометрии, поиска, JSON и learning оставались в одном `SolverImpl`.
Из-за этого любой потребитель получал полный набор реализаций и внешних
зависимостей, а include-checker не мог доказать отсутствие связей между слоями.

## Решение

Разделить реализацию модульного монолита на:

- `SearchContracts` для `SolverKind`, `SolverConfig` и строковых преобразований;
- `GridCore` и `PolygonCore` для состояния, допустимости, кандидатов, objective
  и независимой валидации;
- `Search` для grid/polygon baseline и компараторов;
- `Json` для строгих wire adapters;
- `Learning` для пошаговых сред.

`Json` может зависеть от `SearchContracts`, но не от алгоритмов `Search`.
`Learning` зависит только от geometry/environment Core. Clipper2 остаётся
PRIVATE-зависимостью PolygonCore и используется только candidate generation;
nlohmann/json остаётся PRIVATE-зависимостью Json. Python binding является
адаптером над Search, Json и Learning.

`SolverImpl` становится INTERFACE-агрегатором, а `Solver` сохраняет прежние
плоские заголовки для App/GUI до A6. Canonical headers, namespace, wire formats,
порядок действий и вычисления objective не меняются.

## Последствия

Положительные:

- полигональное обучение развивается без зависимости от JSON и baseline runtime;
- внешние библиотеки локализованы и проверяются автоматически;
- регрессия определяется тестом конкретного владельца;
- A4 может переработать search lifecycle без затрагивания geometry и learning.

Отрицательные:

- увеличивается число static libraries и test executables;
- до A6 сохраняются два compatibility targets;
- интеграционные learning replay tests дополнительно линкуют Search и Json,
  хотя production Learning от них не зависит.

## Откат

Каждый implementation target можно вернуть в INTERFACE aggregate отдельно.
Forwarding headers и неизменные wire-контракты позволяют выполнить откат без
миграции App, GUI, CLI, Python или сохранённых datasets.
