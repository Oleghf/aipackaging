#!/usr/bin/env bash
set -euo pipefail

# Скрипт запускается уже внутри Ubuntu 24.04. Включение Windows Features и
# первая перезагрузка выполняются заранее из повышенного PowerShell.
sudo apt-get update
sudo apt-get install --yes build-essential cmake ninja-build python3.12-venv

python3.12 -m venv .venv
.venv/bin/python -m pip install --upgrade pip
.venv/bin/python -m pip install -e '.[dev,train]'

nvidia-smi
.venv/bin/python -c 'import torch; assert torch.cuda.is_available(), "PyTorch cannot access CUDA"; print(torch.cuda.get_device_name(0))'
cmake --preset linux-headless-tests
cmake --build --preset build-linux-headless-tests
ctest --test-dir build/linux-headless-tests --output-on-failure
.venv/bin/python -m pytest
