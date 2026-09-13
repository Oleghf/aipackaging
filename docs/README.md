# Документация

- [Видение продукта](product-vision.md) — задача, границы, допущения и вопросы.
- [Текущее состояние](current-state.md) — проверенный baseline и ограничения.
- [Архитектура](architecture.md) — целевые компоненты, контракты и инварианты.
- [Архитектурный аудит A0](architecture-audit.md) — фактические зависимости,
  слабые места и приоритеты переработки.
- [Целевая архитектура](target-architecture.md) — границы Core, Search, Learning,
  Json, Application и adapters.
- [План архитектурной переработки](architecture-refactoring-plan.md) — этапы
  A1-A8, критерии и порядок относительно M6.2-M6.3.
- [План разработки](roadmap.md) — этапы и критерии завершения.
- [Качество и оценка](quality.md) — baseline-алгоритмы, данные, метрики и тесты.
- [Обучение M3](m3-training.md) — WSL2, smoke, BC/PPO, export и evaluation.
- [ADR-0007](decisions/0007-polygon-desktop-workspace.md) — полигональная desktop-вкладка, потоки и отмена.
- [ADR-0008](decisions/0008-polygon-dataset-and-benchmark.md) — production polygon dataset v2,
  family isolation, resume и frozen baseline benchmark.
- [CPU smoke-эксперимент M3](experiments/m3-smoke-2026-09-10.md) — проверенный
  инженерный прогон до канонического CUDA-обучения.
- [Канонический polygon dataset v2](experiments/m6-1-polygon-dataset-v2-2026-09-13.md) —
  замороженные хеши, coverage и validation benchmark M6.1.
- [ADR-0001: гибридный решатель](decisions/0001-hybrid-neural-solver.md) — граница
  между нейросетью и детерминированной геометрией.
- [ADR-0002: ценность остатка](decisions/0002-valuable-remnant-objective.md) —
  принятая бизнес-цель и математическая формализация MVP.
- [ADR-0003: клеточный solver-контракт](decisions/0003-grid-solver-contract.md) —
  форматы M1, baseline-алгоритмы, метрики и статусы поиска.
- [ADR-0004: обучаемая среда и датасет](decisions/0004-learning-environment-and-dataset.md) —
  observation/action/reward M2 и воспроизводимый формат данных.
- [ADR-0005: иерархическая политика](decisions/0005-hierarchical-grid-policy.md) —
  BC/PPO, masked hierarchy, ONNX bundle и quality gate M3.
- [ADR-0006: полигональный контракт](decisions/0006-polygon-nesting-contract.md) —
  микронная геометрия, NFP, производственные расстояния и observation M4.
- [ADR-0012: границы Python dataset pipeline](decisions/0012-python-dataset-pipeline-boundaries.md) —
  общая сериализация, независимые grid/polygon pipelines и data-only CLI.

Документы явно разделяют текущее и целевое состояние. Планируемую возможность
нельзя выдавать за уже реализованную.
