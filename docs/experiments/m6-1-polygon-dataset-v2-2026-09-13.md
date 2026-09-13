# Канонический polygon dataset v2 — M6.1

## Результат

Канонический `aipackaging.polygon_dataset` v2 успешно сгенерирован и полностью
проверен. Dataset предназначен для следующего этапа полигонального BC/PPO; test
split при построении текущего benchmark не использовался.

- revision кода и нативного модуля: `1baf53a8c1f0`;
- master seed: `42`;
- задачи: `384` (`256 train`, `64 validation`, `64 test`);
- траектории: `1920`, по пять baseline на задачу;
- tiers: поровну `small` и `medium` внутри каждого split;
- генерация: 10 worker-процессов, `1:02:47` wall-clock;
- SHA-256 manifest:
  `747ADE28A858DE2F1D484CDF6C942DF07CB59AF017EEB886F8C18D9105422C44`.

Dataset verifier заново прочитал все shards, проверил hashes, family isolation,
coverage, expert references и полностью проиграл 1920 траекторий через
динамический C++ action space. Итог: `384` задачи и `1920` корректных
траекторий. Manifest также принят JSON Schema Draft 2020-12.

## Бюджеты baseline

- `input-first-fit`, `area-left-bottom`, `max-side-left-bottom` — один
  детерминированный запуск;
- `random-left-bottom` — 64 итерации;
- `beam` — width 8 и не более 5000 expanded states;
- timeout — 0; wall-clock метрики перед сериализацией обнулены.

## Размеры и хеши shards

| Shard | Records | Bytes | SHA-256 |
|---|---:|---:|---|
| `train-problems.jsonl.gz` | 256 | 69 669 | `6e0caf914a163df3462c1510e41e0c0f3af25cfab30cd7168a378e9a64b4279a` |
| `train-trajectories.jsonl.gz` | 1280 | 255 831 | `b735e448ac50d7a0db2406f87c726268566948f149705c0fa6c22ee2194fda7f` |
| `validation-problems.jsonl.gz` | 64 | 17 829 | `c1cb77cb2efb719347baf7f3a0bbc23c7b604b4a873a10b6949a8b713fd92d9b` |
| `validation-trajectories.jsonl.gz` | 320 | 65 395 | `9755519f4b593f508e3bd5d9bbc2a26d00bd211982ffa987a7a687edef2713fb` |
| `test-problems.jsonl.gz` | 64 | 18 344 | `9e660f4db626b797dfa867beed0a9633efa9e2c9249f04add41632c627fff788` |
| `test-trajectories.jsonl.gz` | 320 | 66 043 | `0ae4c695e9152764243b5cedd0f0fb85b4b41bfd734db51dccb2616fc31009ed` |

Размер `manifest.json` — 161 531 байт. Artifacts и `.work` cache остаются в
игнорируемом каталоге и в Git не входят.

## Coverage

| Tier | Convex | Concave | Arc | Bézier | Hole | Thin | Symmetric |
|---|---:|---:|---:|---:|---:|---:|---:|
| `small` | 176 | 156 | 102 | 108 | 104 | 170 | 160 |
| `medium` | 192 | 192 | 178 | 184 | 174 | 192 | 192 |

Feature tags считаются по типам деталей, поэтому их количества превышают число
задач и не являются взаимоисключающими.

## Validation benchmark

Benchmark построен только из 320 замороженных validation trajectories и не
перезапускал solver. Он содержит 64 задачи. SHA-256 отчёта:
`C366CEF8DE283E5FF14BE52726334D63B4B8ECD36AE79BEB069D55C3BCF4F5CB`;
размер — 146 771 байт.

| Solver | Solved | Completion | Mean placed parts | Mean used length, mm | Expert selections |
|---|---:|---:|---:|---:|---:|
| `input-first-fit` | 64/64 | 100% | 9.78125 | 112.047656 | 0 |
| `area-left-bottom` | 64/64 | 100% | 9.78125 | 103.500141 | 8 |
| `max-side-left-bottom` | 64/64 | 100% | 9.78125 | 105.464484 | 2 |
| `random-left-bottom` | 64/64 | 100% | 9.78125 | 96.552781 | 49 |
| `beam` | 21/64 | 32.8125% | 4.265625 | 59.348531 | 5 |

Средние значения beam включают partial-решения и поэтому не сравниваются
напрямую со средними значениями четырёх полностью успешных baseline. Из 320
solver-результатов 65 эквивалентны лучшему для своей задачи и 255 хуже; один
дополнительный эквивалентный результат отражает реальную ничью, а не второй
expert reference.

Отчёт принят JSON Schema Draft 2020-12. Независимый verifier полностью
пересчитал агрегаты, expert references и отношения качества из frozen
trajectories и принял все 64 задачи и пять solver-сводок.
