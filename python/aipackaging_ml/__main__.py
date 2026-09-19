"""Командная строка датасета, обучения, оценки и экспорта политики."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from .commands import datasets as dataset_commands
from .commands import ml as ml_commands


def _parser() -> argparse.ArgumentParser:
    """Создаёт строгий контракт анализатора аргументов для публичных команд."""

    parser = argparse.ArgumentParser(prog="python -m aipackaging_ml")
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate-dataset", help="создать клеточный набор данных `grid_dataset` v1")
    generate.add_argument("--output", type=Path, default=Path("artifacts/datasets/grid-v1"))
    generate.add_argument("--seed", type=int, default=42)
    generate.add_argument("--workers", type=int, default=1)
    generate.add_argument("--train", type=int, default=dataset_commands.DEFAULT_SPLITS["train"])
    generate.add_argument("--validation", type=int, default=dataset_commands.DEFAULT_SPLITS["validation"])
    generate.add_argument("--test", type=int, default=dataset_commands.DEFAULT_SPLITS["test"])
    generate.add_argument("--tier", choices=("small", "medium"), action="append")
    generate.add_argument("--smoke", action="store_true", help="по одной задаче для каждой выборки и профиля сложности")

    verify = commands.add_parser("verify-dataset", help="проверить манифест, части набора и повторное проигрывание")
    verify.add_argument("path", type=Path)

    polygon_generate = commands.add_parser("generate-polygon-dataset", help="создать полигональный набор данных `polygon_dataset` v2")
    polygon_generate.add_argument("--output", type=Path, default=Path("artifacts/datasets/polygon-v2"))
    polygon_generate.add_argument("--seed", type=int, default=42)
    polygon_generate.add_argument("--workers", type=int, default=1)
    polygon_generate.add_argument("--train", type=int, default=dataset_commands.POLYGON_SPLITS["train"])
    polygon_generate.add_argument("--validation", type=int, default=dataset_commands.POLYGON_SPLITS["validation"])
    polygon_generate.add_argument("--test", type=int, default=dataset_commands.POLYGON_SPLITS["test"])
    polygon_generate.add_argument("--tier", choices=("small", "medium"), action="append")
    polygon_generate.add_argument("--random-iterations", type=int, default=64)
    polygon_generate.add_argument("--beam-width", type=int, default=8)
    polygon_generate.add_argument("--max-expanded-states", type=int, default=5_000)
    polygon_generate.add_argument("--max-attempts", type=int, default=256)
    polygon_generate.add_argument("--resume", action="store_true")
    polygon_generate.add_argument("--smoke", action="store_true", help="по две задачи для каждой выборки и профиля сложности")

    polygon_verify = commands.add_parser("verify-polygon-dataset", help="проверить `polygon_dataset` v1 либо v2")
    polygon_verify.add_argument("path", type=Path)

    polygon_benchmark = commands.add_parser(
        "benchmark-polygon-baselines", help="сравнить сохранённые траектории полигональных базовых алгоритмов"
    )
    polygon_benchmark.add_argument("--dataset", type=Path, default=Path("artifacts/datasets/polygon-v2"))
    polygon_benchmark.add_argument(
        "--output", type=Path, default=Path("artifacts/benchmarks/polygon-v2-validation.json")
    )
    polygon_benchmark.add_argument("--split", choices=("validation", "test"), default="validation")

    train = commands.add_parser("train", help="выполнить воспроизводимый BC и PPO")
    train.add_argument("--config", type=Path, required=True)
    train.add_argument("--dataset", type=Path, required=True)
    train.add_argument("--run-dir", type=Path, required=True)
    train.add_argument("--device", choices=("cuda", "cpu"), default="cuda")
    train.add_argument("--resume", type=Path)
    train.add_argument("--smoke", action="store_true")

    evaluate = commands.add_parser("evaluate", help="сравнить политику с замороженными результатами базовых алгоритмов")
    evaluate.add_argument("--checkpoint", type=Path, required=True)
    evaluate.add_argument("--config", type=Path, required=True)
    evaluate.add_argument("--dataset", type=Path, required=True)
    evaluate.add_argument("--output", type=Path, required=True)
    evaluate.add_argument("--split", choices=("validation", "test"), default="validation")
    evaluate.add_argument("--device", choices=("cuda", "cpu"), default="cpu")
    evaluate.add_argument("--smoke", action="store_true")

    export = commands.add_parser("export-onnx", help="экспортировать комплект политики v1")
    export.add_argument("--checkpoint", type=Path, required=True)
    export.add_argument("--config", type=Path, required=True)
    export.add_argument("--output", type=Path, required=True)
    export.add_argument("--model-id", default="grid-policy-v1")

    model = commands.add_parser("verify-model", help="проверить хеши ONNX и соответствие вычислений")
    model.add_argument("--model", type=Path, required=True)
    model.add_argument("--checkpoint", type=Path, required=True)
    model.add_argument("--config", type=Path, required=True)
    model.add_argument("--dataset", type=Path, required=True)
    model.add_argument("--split", choices=("validation",), default="validation")
    model.add_argument("--smoke", action="store_true")

    polygon_train = commands.add_parser("train-polygon", help="выполнить полигональное BC и PPO")
    polygon_train.add_argument("--config", type=Path, required=True)
    polygon_train.add_argument("--dataset", type=Path, required=True)
    polygon_train.add_argument("--run-dir", type=Path, required=True)
    polygon_train.add_argument("--device", choices=("cuda", "cpu"), default="cuda")
    polygon_train.add_argument("--resume", type=Path)
    polygon_train.add_argument("--smoke", action="store_true")

    polygon_evaluate = commands.add_parser("evaluate-polygon", help="оценить полигональную политику")
    polygon_evaluate.add_argument("--checkpoint", type=Path, required=True)
    polygon_evaluate.add_argument("--config", type=Path, required=True)
    polygon_evaluate.add_argument("--dataset", type=Path, required=True)
    polygon_evaluate.add_argument("--output", type=Path, required=True)
    polygon_evaluate.add_argument("--split", choices=("validation", "test"), default="validation")
    polygon_evaluate.add_argument("--device", choices=("cuda", "cpu"), default="cpu")
    polygon_evaluate.add_argument("--smoke", action="store_true")

    polygon_finalize = commands.add_parser(
        "finalize-polygon-run", help="зафиксировать остановленный запуск без продолжения обучения"
    )
    polygon_finalize.add_argument("--config", type=Path, required=True)
    polygon_finalize.add_argument("--dataset", type=Path, required=True)
    polygon_finalize.add_argument("--run-dir", type=Path, required=True)
    polygon_finalize.add_argument("--elapsed-training-seconds", type=float)
    polygon_finalize.add_argument("--device", choices=("cuda", "cpu"), default="cuda")

    polygon_run = commands.add_parser("verify-polygon-run", help="проверить файлы полигонального обучения")
    polygon_run.add_argument("--run-dir", type=Path, required=True)

    polygon_export = commands.add_parser("export-polygon-onnx", help="экспортировать комплект полигональной модели ONNX")
    polygon_export.add_argument("--checkpoint", type=Path, required=True)
    polygon_export.add_argument("--config", type=Path, required=True)
    polygon_export.add_argument("--output", type=Path, required=True)
    polygon_export.add_argument("--model-id", default="hierarchical-polygon-policy-v1")

    polygon_model = commands.add_parser("verify-polygon-model", help="проверить комплект полигональной модели ONNX")
    polygon_model.add_argument("--model", type=Path, required=True)
    polygon_model.add_argument("--checkpoint", type=Path)
    polygon_model.add_argument("--config", type=Path)
    polygon_model.add_argument("--dataset", type=Path)
    polygon_model.add_argument("--smoke", action="store_true")

    polygon_onnx_evaluate = commands.add_parser("evaluate-polygon-onnx", help="оценить полигональную модель ONNX")
    polygon_onnx_evaluate.add_argument("--model", type=Path, required=True)
    polygon_onnx_evaluate.add_argument("--config", type=Path, required=True)
    polygon_onnx_evaluate.add_argument("--dataset", type=Path, required=True)
    polygon_onnx_evaluate.add_argument("--output", type=Path, required=True)
    polygon_onnx_evaluate.add_argument("--split", choices=("validation", "test"), default="validation")
    polygon_onnx_evaluate.add_argument("--smoke", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Выполняет выбранную команду и печатает машинно-читаемый итог."""

    arguments = _parser().parse_args(argv)
    handlers = {
        "generate-dataset": dataset_commands.generate_grid,
        "verify-dataset": dataset_commands.verify_grid,
        "generate-polygon-dataset": dataset_commands.generate_polygon,
        "verify-polygon-dataset": dataset_commands.verify_polygon,
        "benchmark-polygon-baselines": dataset_commands.benchmark_polygon,
        "train": ml_commands.train,
        "evaluate": ml_commands.evaluate,
        "export-onnx": ml_commands.export_onnx,
        "verify-model": ml_commands.verify_model,
        "train-polygon": ml_commands.train_polygon,
        "evaluate-polygon": ml_commands.evaluate_polygon,
        "finalize-polygon-run": ml_commands.finalize_polygon_run,
        "verify-polygon-run": ml_commands.verify_polygon_run,
        "export-polygon-onnx": ml_commands.export_polygon_onnx,
        "verify-polygon-model": ml_commands.verify_polygon_model,
        "evaluate-polygon-onnx": ml_commands.evaluate_polygon_onnx,
    }
    result = handlers[arguments.command](arguments)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
