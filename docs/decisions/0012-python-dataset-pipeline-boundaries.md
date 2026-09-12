# ADR-0012: границы Python dataset pipeline

Статус: принято.

## Контекст

Grid- и polygon-датасеты независимо реализовывали canonical JSON, gzip, SHA-256,
проверку полей и shard-путей. Модули одновременно отвечали за генерацию задач,
baseline rollout, запись manifest и полный replay. Загрузчик M3 импортировал
закрытую функцию из публичного `dataset.py`, а CLI напрямую связывал data-only и
ML-сценарии. Это повышало риск расхождения форматов и случайного появления
PyTorch в базовой установке.

Production pipeline M6.1 существует только как незавершённый worktree. A5 не
смешивает этот функциональный delta с архитектурным рефакторингом стабильного A4.

## Решение

- Внутренний пакет `aipackaging_ml.datasets` владеет canonical JSON/JSONL,
  deterministic gzip, SHA-256, безопасными shard-путями и атомарным cache I/O.
- Grid и polygon разделены на generation, rollout, replay/verification и сборку
  manifest. Геометрия, action space, validation и comparator остаются в C++.
- `dataset.py`, `generator.py`, `polygon_dataset.py`, `contracts.py` и
  `environment.canonical_json` сохраняют прежние import paths как фасады.
- `training_data` читает shards через общий публичный внутренний reader, а не
  через закрытое имя фасада.
- CLI handlers разделены на dataset и ML. ML-зависимости импортируются только
  после выбора соответствующей команды.
- Architecture checker определяет Python-владельца по полному module path и
  самому длинному prefix. Data common не зависит от native/environment/training,
  а dataset-контур не зависит от model/training/evaluation или PyTorch.
- Cache v1 является внутренним envelope с identity, fingerprint и payload. A5
  проверяет его атомарность, но не добавляет `--resume` в публичный CLI.

## Последствия

Grid/polygon v1 сохраняют сигнатуры, команды и точные bytes при одинаковой build
revision. Базовая установка продолжает требовать только NumPy и native binding.
Production dataset v2, resume и benchmark M6.1 должны подключаться к выделенным
модулям без повторной реализации JSON/gzip/hash/cache.

Откат возможен заменой тонких фасадов прежними монолитными реализациями: wire-
форматы и C++ bindings не изменены.
