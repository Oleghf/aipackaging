"""Командная строка генерации и проверки клеточного датасета v1."""

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
    result = verify_dataset(arguments.path)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
