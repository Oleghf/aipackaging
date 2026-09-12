"""Детерминированная генерация smoke-задач и production-семейств polygon dataset."""

from __future__ import annotations

import hashlib
import random
from typing import Any, Mapping

from .recipes import POLYGON_FEATURES, scale_path, scaled_shape, shape

POLYGON_GENERATOR_VERSION = 2
POLYGON_GENERATOR_REVISION = "2.0.0"
POLYGON_TIERS = ("small", "medium")
POLYGON_PROFILES: dict[str, dict[str, list[int]]] = {
    "small": {
        "sheetWidth": [100, 220],
        "sheetHeight": [80, 160],
        "partTypes": [2, 5],
        "instances": [4, 10],
        "partExtent": [10, 45],
    },
    "medium": {
        "sheetWidth": [300, 600],
        "sheetHeight": [200, 400],
        "partTypes": [5, 10],
        "instances": [12, 30],
        "partExtent": [20, 100],
    },
}


def derive_seed(*parts: object) -> int:
    """Выводит независимый 64-битный seed из стабильной последовательности компонентов."""

    return int.from_bytes(hashlib.sha256(":".join(map(str, parts)).encode("utf-8")).digest()[:8], "big")


def generate_polygon_problem(master_seed: int, split: str, index: int) -> tuple[dict[str, Any], int]:
    """Сохраняет прежнюю генерацию одной полностью помещающейся smoke-задачи M4."""

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
        "parts": [{
            "id": f"shape-{kind}",
            "quantity": 2,
            "outer": outer,
            "holes": holes,
            "allowedRotations": [0, 90, 180, 270],
        }],
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    return problem, seed


def family_recipe(master_seed: int, tier: str, split: str, family_index: int) -> dict[str, Any]:
    """Создаёт общую топологию, повороты и количества двух вариантов семьи."""

    if tier not in POLYGON_PROFILES:
        raise ValueError(f"unknown polygon tier: {tier}")
    profile = POLYGON_PROFILES[tier]
    rng = random.Random(derive_seed(master_seed, "polygon-family-v2", tier, split, family_index))
    type_count = rng.randint(*profile["partTypes"])
    # Число экземпляров держится около нижней границы: пять NFP-baseline на
    # production-наборе иначе делают подготовку данных непропорционально долгой.
    minimum_instances = max(type_count, profile["instances"][0])
    total_instances = rng.randint(minimum_instances, min(profile["instances"][1], minimum_instances + 3))
    quantities = [1] * type_count
    for _ in range(total_instances - type_count):
        quantities[rng.randrange(type_count)] += 1

    parts = []
    feature_offset = derive_seed(master_seed, tier, split, family_index) % len(POLYGON_FEATURES)
    minimum, maximum = profile["partExtent"]
    for index in range(type_count):
        kind = POLYGON_FEATURES[(feature_offset + index) % len(POLYGON_FEATURES)]
        # Базовый размер рассчитан под точные варианты 2x/3x, поэтому оба
        # результата остаются внутри declared tier.
        width = round(rng.uniform(minimum / 2.0, maximum / 3.0), 3)
        height = round(rng.uniform(minimum / 2.0, maximum / 3.0), 3)
        if kind == "arc":
            width = height = min(width, height)
        elif kind == "thin":
            height = round(min(width / 5.0, height), 3)
        rotations = rng.choice(([0], [0, 90], [0, 90, 180, 270]))
        parts.append({
            "kind": kind,
            "width": width,
            "height": height,
            "quantity": quantities[index],
            "rotations": list(rotations),
        })
    return {"parts": parts}


def _path_points(path: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    """Собирает все аналитические точки пути для вычисления его bounding box."""

    result = [path["start"]]
    for segment in path["segments"]:
        result.extend(segment[key] for key in ("end", "center", "control1", "control2") if key in segment)
    return result


def hidden_shelf_layout(
    problem: Mapping[str, Any], extents: Mapping[str, tuple[float, float]]
) -> list[dict[str, Any]] | None:
    """Строит полную скрытую раскладку bounding boxes горизонтальными полками."""

    margin = float(problem["manufacturing"]["sheetMargin"])
    spacing = float(problem["manufacturing"]["partSpacing"])
    sheet_width = float(problem["sheet"]["width"])
    sheet_height = float(problem["sheet"]["height"])
    instances = []
    for part in problem["parts"]:
        width, height = extents[part["id"]]
        for instance_index in range(part["quantity"]):
            instances.append((part["id"], instance_index, width, height))
    instances.sort(key=lambda item: (-item[3], -item[2], item[0], item[1]))

    x = margin
    y = margin
    row_height = 0.0
    placements = []
    for part_id, instance_index, width, height in instances:
        if x + width > sheet_width - margin + 1e-9:
            x = margin
            y += row_height + spacing
            row_height = 0.0
        if y + height > sheet_height - margin + 1e-9:
            return None
        placements.append({
            "partId": part_id,
            "instanceIndex": instance_index,
            "xMicrometers": round(x * 1000),
            "yMicrometers": round(y * 1000),
            "rotationDegrees": 0,
        })
        x += width + spacing
        row_height = max(row_height, height)
    return placements


def generate_family_variant(
    master_seed: int,
    tier: str,
    split: str,
    family_index: int,
    variant: int,
    attempt: int,
) -> tuple[dict[str, Any], list[dict[str, Any]] | None, int]:
    """Создаёт одну scale-вариацию production-семьи и её скрытую раскладку."""

    if tier not in POLYGON_PROFILES or split not in {"train", "validation", "test"}:
        raise ValueError("invalid polygon family address")
    if variant not in (0, 1) or attempt < 0:
        raise ValueError("invalid polygon family variant or attempt")
    profile = POLYGON_PROFILES[tier]
    recipe = family_recipe(master_seed, tier, split, family_index)
    seed = derive_seed(master_seed, "polygon-task-v2", tier, split, family_index, variant, attempt)
    rng = random.Random(seed)
    scale = (2.0, 3.0)[variant]
    margin = round(rng.uniform(2.0, 5.0 if tier == "small" else 8.0), 3)
    spacing = round(rng.uniform(0.5, 1.5 if tier == "small" else 3.0), 3)
    sheet_width = round(rng.uniform(*profile["sheetWidth"]), 3)
    sheet_height = round(rng.uniform(*profile["sheetHeight"]), 3)

    parts = []
    extents: dict[str, tuple[float, float]] = {}
    for index, item in enumerate(recipe["parts"]):
        base_outer, base_holes = scaled_shape(item["kind"], item["width"], item["height"])
        outer = scale_path(base_outer, scale)
        holes = [scale_path(path, scale) for path in base_holes]
        points = _path_points(outer)
        width = max(float(value["x"]) for value in points) - min(float(value["x"]) for value in points)
        height = max(float(value["y"]) for value in points) - min(float(value["y"]) for value in points)
        part_id = f"part-{index:02d}-{item['kind']}"
        parts.append({
            "id": part_id,
            "quantity": item["quantity"],
            "outer": outer,
            "holes": holes,
            "allowedRotations": item["rotations"],
        })
        extents[part_id] = (width, height)

    problem = {
        "format": "aipackaging.polygon_problem",
        "version": 1,
        "problemId": f"polygon-v2-{tier}-{split}-f{family_index:04d}-v{variant}-{seed:016x}",
        "sheet": {"width": sheet_width, "height": sheet_height, "unit": "mm"},
        "manufacturing": {
            "sheetMargin": margin,
            "partSpacing": spacing,
            "kerf": round(rng.uniform(0.1, 0.4), 3),
            "curveTolerance": 0.05,
        },
        "parts": parts,
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }
    return problem, hidden_shelf_layout(problem, extents), seed
