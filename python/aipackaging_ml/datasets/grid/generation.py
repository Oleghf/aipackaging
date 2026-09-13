"""Детерминированная генерация гарантированно размещаемых задач с полимино."""

from __future__ import annotations

import hashlib
import json
import random
from dataclasses import dataclass
from typing import Any, Iterable


@dataclass(frozen=True)
class GenerationProfile:
    """Задаёт закрытые диапазоны одного публичного профиля генератора v1."""

    sheet_min: int
    sheet_max: int
    type_min: int
    type_max: int
    instance_min: int
    instance_max: int
    part_area_min: int
    part_area_max: int
    utilization_min: float
    utilization_max: float


PROFILES = {
    "small": GenerationProfile(8, 12, 2, 5, 4, 10, 1, 6, 0.45, 0.75),
    "medium": GenerationProfile(16, 24, 4, 10, 12, 30, 2, 12, 0.55, 0.85),
}


def derive_seed(master_seed: int, tier: str, split: str, index: int, attempt: int) -> int:
    """Выводит независимое 64-битное начальное значение из стабильного адреса задачи."""

    source = f"{master_seed}|{tier}|{split}|{index}|{attempt}".encode("utf-8")
    return int.from_bytes(hashlib.sha256(source).digest()[:8], "big")


def _normalize(cells: Iterable[tuple[int, int]]) -> tuple[tuple[int, int], ...]:
    """Переносит клетки к началу координат и возвращает стабильную сортировку."""

    values = tuple(cells)
    min_column = min(column for column, _ in values)
    min_row = min(row for _, row in values)
    return tuple(sorted((column - min_column, row - min_row) for column, row in values))


def _rotate(cells: Iterable[tuple[int, int]]) -> tuple[tuple[int, int], ...]:
    """Поворачивает полимино на 90 градусов и повторно нормализует его."""

    return _normalize((-row, column) for column, row in cells)


def unique_orientations(cells: Iterable[tuple[int, int]]) -> tuple[tuple[tuple[int, int], ...], ...]:
    """Возвращает уникальные повороты формы в порядке 0/90/180/270."""

    current = _normalize(cells)
    result: list[tuple[tuple[int, int], ...]] = []
    for _ in range(4):
        if current not in result:
            result.append(current)
        current = _rotate(current)
    return tuple(result)


def _has_hole(cells: tuple[tuple[int, int], ...]) -> bool:
    """Ищет ограниченную пустую 4-связную компоненту внутри ограничивающей рамки формы."""

    occupied = set(cells)
    width = max(column for column, _ in cells) + 1
    height = max(row for _, row in cells) + 1
    outside: set[tuple[int, int]] = set()
    queue = [(-1, -1)]
    while queue:
        cell = queue.pop()
        if cell in outside or cell in occupied:
            continue
        column, row = cell
        if column < -1 or row < -1 or column > width or row > height:
            continue
        outside.add(cell)
        queue.extend(
            ((column - 1, row), (column + 1, row), (column, row - 1), (column, row + 1))
        )
    return any((column, row) not in occupied and (column, row) not in outside for row in range(height) for column in range(width))


def grow_polyomino(rng: random.Random, area: int) -> tuple[tuple[int, int], ...]:
    """Выращивает 4-связную форму заданной площади без внутренних отверстий."""

    for _ in range(128):
        cells = {(0, 0)}
        while len(cells) < area:
            frontier: set[tuple[int, int]] = set()
            for column, row in cells:
                frontier.update(((column - 1, row), (column + 1, row), (column, row - 1), (column, row + 1)))
            frontier.difference_update(cells)
            cells.add(rng.choice(sorted(frontier)))
        normalized = _normalize(cells)
        if not _has_hole(normalized):
            return normalized
    raise RuntimeError("не удалось вырастить полимино без отверстий")


def family_hash(problem: dict[str, Any]) -> str:
    """Вычисляет хеш набора фигур и их количеств, неизменный к повороту."""

    signatures = []
    for part in problem["parts"]:
        cells = tuple((cell["column"], cell["row"]) for cell in part["cells"])
        canonical_shape = min(unique_orientations(cells))
        # Семейство определяется только геометрией набора типов: изменение
        # количества экземпляров не должно позволять форме перейти в другую выборку.
        signatures.append(canonical_shape)
    payload = json.dumps(sorted(signatures), ensure_ascii=False, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _try_place(
    occupied: set[tuple[int, int]], cells: tuple[tuple[int, int], ...], columns: int, rows: int
) -> set[tuple[int, int]] | None:
    """Находит первое допустимое скрытое размещение по строке, столбцу и ориентации."""

    for orientation in unique_orientations(cells):
        width = max(column for column, _ in orientation) + 1
        height = max(row for _, row in orientation) + 1
        for row in range(rows - height + 1):
            for column in range(columns - width + 1):
                placed = {(column + x, row + y) for x, y in orientation}
                if occupied.isdisjoint(placed):
                    return placed
    return None


def generate_problem(master_seed: int, tier: str, split: str, index: int, attempt: int = 0) -> dict[str, Any]:
    """Создаёт одну задачу профиля; скрытая раскладка гарантирует её разрешимость."""

    if tier not in PROFILES:
        raise ValueError(f"неизвестный профиль сложности: {tier}")
    profile = PROFILES[tier]
    rng = random.Random(derive_seed(master_seed, tier, split, index, attempt))
    columns = rng.randint(profile.sheet_min, profile.sheet_max)
    rows = rng.randint(profile.sheet_min, profile.sheet_max)
    sheet_area = columns * rows
    type_count = rng.randint(profile.type_min, profile.type_max)

    # Для крупных листов генерация смещает площадь вверх, иначе физически нельзя
    # достигнуть нижней границы использования материала при лимите в 30 экземпляров.
    minimum_average = profile.utilization_min * sheet_area / profile.instance_max
    areas = [
        rng.randint(max(profile.part_area_min, int(minimum_average)), profile.part_area_max)
        for _ in range(type_count)
    ]
    shapes: list[tuple[tuple[int, int], ...]] = []
    seen_shapes: set[tuple[tuple[int, int], ...]] = set()
    for area in areas:
        for _ in range(64):
            shape = grow_polyomino(rng, area)
            signature = min(unique_orientations(shape))
            if signature not in seen_shapes:
                seen_shapes.add(signature)
                shapes.append(shape)
                break
        else:
            raise RuntimeError("не удалось получить уникальные типы фигур")

    quantities = [0] * type_count
    occupied: set[tuple[int, int]] = set()
    target = rng.uniform(profile.utilization_min, profile.utilization_max) * sheet_area
    candidates = list(range(type_count))
    rng.shuffle(candidates)
    # Каждый тип обязан войти хотя бы один раз, чтобы список деталей не содержал нулевое количество.
    order = candidates + [rng.randrange(type_count) for _ in range(profile.instance_max - type_count)]
    for type_index in order:
        placed = _try_place(occupied, shapes[type_index], columns, rows)
        if placed is None:
            continue
        occupied.update(placed)
        quantities[type_index] += 1
        instance_count = sum(quantities)
        if instance_count >= profile.instance_min and len(occupied) >= target:
            break

    utilization = len(occupied) / sheet_area
    instance_count = sum(quantities)
    if (
        any(quantity == 0 for quantity in quantities)
        or not profile.instance_min <= instance_count <= profile.instance_max
        or not profile.utilization_min <= utilization <= profile.utilization_max
    ):
        raise RuntimeError("попытка не достигла диапазона профиля")

    parts = []
    for type_index, (shape, quantity) in enumerate(zip(shapes, quantities, strict=True)):
        parts.append(
            {
                "id": f"part-{type_index:02d}",
                "quantity": quantity,
                "cells": [{"column": column, "row": row} for column, row in shape],
                "allowedRotations": [0, 90, 180, 270],
            }
        )
    return {
        "format": "aipackaging.grid_problem",
        "version": 1,
        "problemId": f"grid-v1-{tier}-{split}-{index:04d}",
        "sheet": {"columns": columns, "rows": rows, "unit": "cell"},
        "parts": parts,
        "objective": {"type": "valuable_right_remnant", "version": 1},
    }


def generate_unique_problem(
    master_seed: int, tier: str, split: str, index: int, family_splits: dict[str, str]
) -> tuple[dict[str, Any], int]:
    """Повторяет адресуемые попытки до корректного профиля без утечки между выборками."""

    for attempt in range(10_000):
        try:
            problem = generate_problem(master_seed, tier, split, index, attempt)
        except RuntimeError:
            continue
        signature = family_hash(problem)
        owner = family_splits.get(signature)
        if owner is None or owner == split:
            family_splits[signature] = split
            return problem, derive_seed(master_seed, tier, split, index, attempt)
    raise RuntimeError(f"исчерпаны попытки генерации {tier}/{split}/{index}")
