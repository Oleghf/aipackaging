# ADR-0008: полигональный датасет и baseline benchmark

Статус: принято. Дата: 2026-09-12.

## Контекст

Smoke-набор M4 проверяет связность среды, но недостаточен для обучения и не
фиксирует распределение сложности. Черновик M6.1 существовал до декомпозиции A5
и повторно объединял генерацию, сериализацию, cache, replay и verification.

## Решение

`aipackaging.polygon_dataset` v2 хранит profiles `small`/`medium`, derived seeds,
две равномерно масштабированные вариации семьи, rotation-invariant family hash,
coverage, rejection attempts, budgets и SHA-256 shards. Обе вариации принадлежат
одному split. Скрытая shelf-раскладка проверяется точным `PolygonEnvironment` и
не записывается в обучающие данные.

Каждая принятая задача содержит пять `polygon_trajectory` v1. Wall-clock метрики
обнуляются, trajectories повторно проигрываются через динамический C++ action
space, а expert выбирается общим C++-компаратором только среди полных решений.
Форматы problem, solution и trajectory остаются v1; dataset v1 читается.

Cache использует общий envelope A5 с identity задачи и fingerprint всей
конфигурации. Shards записываются общей canonical JSONL/gzip инфраструктурой.
`polygon_benchmark_report` v1 строится только из frozen trajectories, различает
равное лучшему и худшее качество и связывается с SHA-256 manifest.

## Последствия

- multiprocessing и resume не меняют опубликованные bytes;
- test split используется benchmark только по явной команде;
- artifacts и cache не коммитятся;
- M6.1 завершается только после полного прогона 384 задач, verify и validation
  benchmark с зафиксированным manifest SHA-256;
- обучение и ONNX остаются этапами M6.2–M6.3.

## Фактическая фиксация

Completion gate пройден 2026-09-13 на revision `1baf53a8c1f0`. Dataset содержит
384 задачи и 1920 траекторий. SHA-256 канонического manifest:
`747ADE28A858DE2F1D484CDF6C942DF07CB59AF017EEB886F8C18D9105422C44`.
Полные параметры, размеры shards и validation benchmark зафиксированы в
[отчёте M6.1](../experiments/m6-1-polygon-dataset-v2-2026-09-13.md).
