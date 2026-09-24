# Клеточный исследовательский контур M3

Статус: инфраструктура обучения сохранена для воспроизводимости, но исходный
критерий качества M3 не закрыт. Рабочая полигональная политика обучается и
поставляется по руководствам M6.2–M6.3.

## Подготовка Windows или WSL2

На рабочем стенде проверены Windows, Python 3.13, официальная CUDA-сборка PyTorch
2.14.0 и RTX 4060. WSL2 с Ubuntu 24.04 и Python 3.12 остаётся переносимым
вариантом, но не является обязательным условием контракта обучения.

Из PowerShell, запущенного от имени администратора:

```powershell
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
```

После перезагрузки:

```powershell
wsl.exe --update
wsl.exe --install -d Ubuntu-24.04
```

Откройте каталог репозитория внутри WSL и выполните:

```bash
bash scripts/bootstrap-m3-wsl.sh
```

Успешная подготовка должна показать RTX 4060 в `nvidia-smi`, собрать без графического интерфейса
C++-тесты и выполнить Python pytest. Локальная `.venv`, сборки, контрольные точки и
модели игнорируются Git.

## Пробный проход

```bash
.venv/bin/python -m aipackaging_ml train \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 \
  --run-dir artifacts/runs/m3-smoke \
  --device cuda --smoke

.venv/bin/python -m aipackaging_ml export-onnx \
  --checkpoint artifacts/runs/m3-smoke/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --output artifacts/models/m3-smoke

.venv/bin/python -m aipackaging_ml verify-model \
  --model artifacts/models/m3-smoke \
  --checkpoint artifacts/runs/m3-smoke/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 --smoke
```

## Канонический запуск

```bash
.venv/bin/python -m aipackaging_ml train \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 \
  --run-dir artifacts/runs/m3-grid-policy-v1 \
  --device cuda

.venv/bin/python -m aipackaging_ml evaluate \
  --checkpoint artifacts/runs/m3-grid-policy-v1/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 \
  --split validation \
  --output artifacts/runs/m3-grid-policy-v1/validation-report.json \
  --device cuda
```

Тестовая выборка запускается один раз после выбора контрольной точки по
проверочной выборке. Её отчёт сохраняется отдельно. Контрольная точка считается
доверенным локальным артефактом:
`torch.load` не применяется к полученным извне файлам.

```bash
.venv/bin/python -m aipackaging_ml evaluate \
  --checkpoint artifacts/runs/m3-grid-policy-v1/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 \
  --split test \
  --output artifacts/runs/m3-grid-policy-v1/test-report.json \
  --device cuda

.venv/bin/python -m aipackaging_ml export-onnx \
  --checkpoint artifacts/runs/m3-grid-policy-v1/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --output artifacts/models/grid-policy-v1

.venv/bin/python -m aipackaging_ml verify-model \
  --model artifacts/models/grid-policy-v1 \
  --checkpoint artifacts/runs/m3-grid-policy-v1/ppo-best.pt \
  --config configs/m3/grid-policy-v1.json \
  --dataset artifacts/datasets/grid-v1 --split validation
```
