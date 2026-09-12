"""Детерминированная генерация задач полигонального smoke-датасета v1."""

from __future__ import annotations

import hashlib
from typing import Any

from .recipes import shape


def generate_polygon_problem(master_seed: int, split: str, index: int) -> tuple[dict[str, Any], int]:
    """Строит одну полностью помещающуюся задачу из независимого derived seed."""

    seed_bytes = f"{master_seed}:polygon:{split}:{index}".encode()
    seed = int.from_bytes(hashlib.sha256(seed_bytes).digest()[:8], "big")
    kind = seed % 5
    scale = 0.25 + ((seed >> 8) % 24) * 0.125
    outer, holes = shape(kind, scale)
    problem = {
        "format": "aipackaging.polygon_problem",
        "version": 1,
        "problemId": f"polygon-{split}-{index:03d}-{seed:016x}",
        "sheet": {"width": 100.0, "height": 70.0, "unit": "mm"},
        "manufacturing": {
            "sheetMargin": 2.0,
            "partSpacing": 1.0,
            "kerf": 0.2,
            "curveTolerance": 0.05,
        },
        "parts": [
            {
                "id": f"shape-{kind}",
                "quantity": 2,
                "outer": outer,
                "holes": holes,
                "allowedRotations": [0, 90, 180, 270],
            }
        ],
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    return problem, seed
