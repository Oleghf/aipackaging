# Обучение полигональной политики M6.2

## Подготовка

Для канонического запуска используется Python 3.13, PyTorch 2.14.0 с CUDA и
замороженный набор M6.1. Его манифест должен иметь SHA-256
`747ade28a858de2f1d484cdf6c942df07cb59af017eeb886f8c18d9105422c44`.

```powershell
python -m pip install -e ".[dev,train]"
python -m aipackaging_ml train-polygon `
  --config configs/m6/polygon-policy-v1.json `
  --dataset artifacts/datasets/polygon-v2 `
  --run-dir artifacts/runs/m6-2-canonical `
  --device cuda
```

Восемь часов являются общим бюджетом эксперимента, а не бюджетом каждого
отдельного процесса. Накопленное время записывается в новые контрольные точки.
Продолжение использует файл `polygon-resume.pt` из того же каталога и получает
только остаток общего бюджета:

```powershell
python -m aipackaging_ml train-polygon `
  --config configs/m6/polygon-policy-v1.json `
  --dataset artifacts/datasets/polygon-v2 `
  --run-dir artifacts/runs/m6-2-canonical `
  --device cuda `
  --resume artifacts/runs/m6-2-canonical/polygon-resume.pt
```

Старые контрольные точки без накопленного времени разрешено оценивать и
фиксировать, но не продолжать. Остановленный извне запуск фиксируется без
изменения весов отдельной командой:

```powershell
python -m aipackaging_ml finalize-polygon-run `
  --config configs/m6/polygon-policy-v1.json `
  --dataset artifacts/datasets/polygon-v2 `
  --run-dir artifacts/runs/m6-2-canonical `
  --elapsed-training-seconds 28800 `
  --device cuda
python -m aipackaging_ml verify-polygon-run `
  --run-dir artifacts/runs/m6-2-canonical
```

Перед BC конвейер создаёт проверяемый `polygon-observation-cache.zip`, связанный
с SHA-256 набора данных, конфигурацией наблюдения и версией нативного модуля.
Геометрические переходы PPO выполняются в четырёх рабочих процессах, а модель
обучается в единственном процессе на CUDA. Каждая контрольная точка сопровождается
файлом `<имя>.sha256`; продолжение отклоняется до десериализации, если хеш не
совпадает.

## Быстрая проверка

```powershell
python -m aipackaging_ml train-polygon `
  --config configs/m6/polygon-policy-v1.json `
  --dataset artifacts/datasets/polygon-v2 `
  --run-dir artifacts/runs/m6-2-smoke `
  --device cuda `
  --smoke
python -m aipackaging_ml verify-polygon-run `
  --run-dir artifacts/runs/m6-2-smoke
```

Проверочная оценка не читает тестовую выборку:

```powershell
python -m aipackaging_ml evaluate-polygon `
  --checkpoint artifacts/runs/m6-2-canonical/polygon-ppo-best.pt `
  --config configs/m6/polygon-policy-v1.json `
  --dataset artifacts/datasets/polygon-v2 `
  --output artifacts/runs/m6-2-canonical/validation.json `
  --split validation `
  --device cuda
```

Тестовая оценка выполняется один раз после выбора контрольной точки. Все
контрольные точки, отчёты и промежуточные файлы остаются в `artifacts/`.

Фактический канонический запуск завершён 16 сентября 2026 года и прошёл
критерий качества. Его хеши и метрики приведены в
[отчёте M6.2](experiments/m6-2-polygon-policy-2026-09-16.md).
