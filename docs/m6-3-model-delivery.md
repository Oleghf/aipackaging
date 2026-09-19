# Поставка полигональной модели M6.3

Комплект модели является внешним артефактом и не входит в Git. Канонический
каталог содержит ровно три файла:

```text
polygon-policy-v1/
  metadata.json
  encoder.onnx
  placement-head.onnx
```

`metadata.json` имеет формат `aipackaging.polygon_policy` v2. Перед созданием
сеансов приложение проверяет архитектуру, набор операций, версии контрактов,
размеры растров и SHA-256 обоих графов. Изменённый или неполный комплект не
принимается.

## Экспорт и проверка

```powershell
python -m aipackaging_ml export-polygon-onnx `
  --checkpoint artifacts/runs/m6-2-canonical/polygon-ppo-best.pt `
  --config artifacts/runs/m6-2-canonical/training-config.json `
  --output artifacts/models/polygon-policy-v1

python -m aipackaging_ml verify-polygon-model `
  --model artifacts/models/polygon-policy-v1
```

Для численной проверки дополнительно передаются `--checkpoint`, `--config` и
`--dataset`. Тогда сравниваются оценки и жадные действия PyTorch/ONNX; допустимое
абсолютное расхождение равно `1e-5`.

## Использование без графического интерфейса

```powershell
AIPackaging_Cli validate-model --input artifacts/models/polygon-policy-v1

AIPackaging_Cli solve `
  --input problem.json `
  --output solution.json `
  --solver neural-best-of `
  --model artifacts/models/polygon-policy-v1 `
  --rollouts 16 `
  --timeout-ms 300000
```

Допустимые режимы: `neural-greedy`, `neural-best-of` и `hybrid`. Гибридный режим
сравнивает нейросетевой результат с `random-left-bottom/64` общим точным
компаратором.

## Настольное приложение

Во вкладке «Полигональный раскрой» пользователь выбирает каталог кнопкой
«Загрузить модель». Последний успешно проверенный путь сохраняется через
`QSettings` и повторно проверяется при следующем запуске. Без корректной модели
все пять базовых алгоритмов доступны, а нейросетевые режимы заблокированы.

Официальная CPU-библиотека ONNX Runtime 1.29.0 должна находиться рядом с
приложением. CMake копирует необходимую DLL автоматически. Для локального
комплекта разработчика можно задать `AIPACKAGING_ONNXRUNTIME_ROOT`.
