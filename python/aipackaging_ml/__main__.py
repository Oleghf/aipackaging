"""Командная строка датасета, обучения, оценки и экспорта политики."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Sequence

from .dataset import DEFAULT_SPLITS, generate_dataset, verify_dataset


def _parser() -> argparse.ArgumentParser:
    """Создаёт строгий argparse-контракт обеих публичных команд."""

    parser = argparse.ArgumentParser(prog="python -m aipackaging_ml")
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate-dataset", help="сгенерировать grid_dataset v1")
    generate.add_argument("--output", type=Path, default=Path("artifacts/datasets/grid-v1"))
    generate.add_argument("--seed", type=int, default=42)
    generate.add_argument("--workers", type=int, default=1)
    generate.add_argument("--train", type=int, default=DEFAULT_SPLITS["train"])
    generate.add_argument("--validation", type=int, default=DEFAULT_SPLITS["validation"])
    generate.add_argument("--test", type=int, default=DEFAULT_SPLITS["test"])
    generate.add_argument("--tier", choices=("small", "medium"), action="append")
    generate.add_argument("--smoke", action="store_true", help="по одной задаче каждого split и tier")

    verify = commands.add_parser("verify-dataset", help="проверить manifest, shards и replay")
    verify.add_argument("path", type=Path)

    train = commands.add_parser("train", help="выполнить воспроизводимый BC и PPO")
    train.add_argument("--config", type=Path, required=True)
    train.add_argument("--dataset", type=Path, required=True)
    train.add_argument("--run-dir", type=Path, required=True)
    train.add_argument("--device", choices=("cuda", "cpu"), default="cuda")
    train.add_argument("--resume", type=Path)
    train.add_argument("--smoke", action="store_true")

    evaluate = commands.add_parser("evaluate", help="сравнить policy с замороженными baseline")
    evaluate.add_argument("--checkpoint", type=Path, required=True)
    evaluate.add_argument("--config", type=Path, required=True)
    evaluate.add_argument("--dataset", type=Path, required=True)
    evaluate.add_argument("--output", type=Path, required=True)
    evaluate.add_argument("--split", choices=("validation", "test"), default="validation")
    evaluate.add_argument("--device", choices=("cuda", "cpu"), default="cpu")
    evaluate.add_argument("--smoke", action="store_true")

    export = commands.add_parser("export-onnx", help="экспортировать policy bundle v1")
    export.add_argument("--checkpoint", type=Path, required=True)
    export.add_argument("--config", type=Path, required=True)
    export.add_argument("--output", type=Path, required=True)
    export.add_argument("--model-id", default="grid-policy-v1")

    model = commands.add_parser("verify-model", help="проверить ONNX hashes и parity")
    model.add_argument("--model", type=Path, required=True)
    model.add_argument("--checkpoint", type=Path, required=True)
    model.add_argument("--config", type=Path, required=True)
    model.add_argument("--dataset", type=Path, required=True)
    model.add_argument("--split", choices=("validation",), default="validation")
    model.add_argument("--smoke", action="store_true")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Выполняет выбранную команду и печатает машинно-читаемый итог."""

    arguments = _parser().parse_args(argv)
    if arguments.command == "generate-dataset":
        split_sizes = (
            {"train": 1, "validation": 1, "test": 1}
            if arguments.smoke
            else {"train": arguments.train, "validation": arguments.validation, "test": arguments.test}
        )
        manifest = generate_dataset(
            arguments.output,
            master_seed=arguments.seed,
            tiers=tuple(arguments.tier or ("small", "medium")),
            split_sizes=split_sizes,
            workers=arguments.workers,
        )
        print(json.dumps({"manifest": str(arguments.output / "manifest.json"), "shards": len(manifest["shards"])}))
        return 0
    if arguments.command == "verify-dataset":
        result = verify_dataset(arguments.path)
        print(json.dumps(result, sort_keys=True))
        return 0
    if arguments.command == "train":
        from .training import train_pipeline

        result = train_pipeline(
            arguments.config,
            arguments.dataset,
            arguments.run_dir,
            device_name=arguments.device,
            smoke=arguments.smoke,
            resume=arguments.resume,
        )
    elif arguments.command == "evaluate":
        from .evaluation import evaluate_checkpoint

        result = evaluate_checkpoint(
            arguments.checkpoint,
            arguments.config,
            arguments.dataset,
            arguments.output,
            split=arguments.split,
            device_name=arguments.device,
            smoke=arguments.smoke,
        )
    elif arguments.command == "export-onnx":
        from .exporting import export_policy_bundle

        result = export_policy_bundle(arguments.checkpoint, arguments.config, arguments.output, model_id=arguments.model_id)
    else:
        from .evaluation import verify_model_bundle

        result = verify_model_bundle(
            arguments.model,
            arguments.checkpoint,
            arguments.config,
            arguments.dataset,
            split=arguments.split,
            smoke=arguments.smoke,
        )
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
