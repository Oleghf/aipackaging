"""Анализ геометрических семейств и заявленного профиля полигонального набора данных."""

from __future__ import annotations

import hashlib
import math
from typing import Any, Iterable, Mapping

from ..serialization import canonical_json
from .generation import POLYGON_PROFILES


def path_points(path: Mapping[str, Any]) -> Iterable[Mapping[str, Any]]:
    """Перечисляет опорные, центральные и контрольные точки аналитического пути."""

    yield path["start"]
    for segment in path["segments"]:
        for key in ("end", "center", "control1", "control2"):
            if key in segment:
                yield segment[key]


def _rotate(value: Mapping[str, Any], rotation: int) -> tuple[float, float]:
    """Поворачивает точку на один из четырёх разрешённых углов вокруг начала координат."""

    x, y = float(value["x"]), float(value["y"])
    if rotation == 90:
        return -y, x
    if rotation == 180:
        return -x, -y
    if rotation == 270:
        return y, -x
    return x, y


def _normalized_point(
    value: Mapping[str, Any], rotation: int, minimum_x: float, minimum_y: float, scale: float
) -> dict[str, float]:
    """Переводит повёрнутую точку к началу координат и единому масштабу семьи."""

    x, y = _rotate(value, rotation)
    return {"x": round((x - minimum_x) / scale, 6), "y": round((y - minimum_y) / scale, 6)}


def _normalized_part(part: Mapping[str, Any], rotation: int) -> dict[str, Any]:
    """Возвращает геометрию детали без идентификаторов в выбранной ориентации."""

    paths = [part["outer"], *part["holes"]]
    rotated = [_rotate(point, rotation) for path in paths for point in path_points(path)]
    minimum_x = min(point[0] for point in rotated)
    minimum_y = min(point[1] for point in rotated)
    width = max(point[0] for point in rotated) - minimum_x
    height = max(point[1] for point in rotated) - minimum_y
    # Единый коэффициент сохраняет отношение сторон и отличает разные семейства.
    scale = max(width, height, 1e-12)

    def normalize_path(path: Mapping[str, Any]) -> dict[str, Any]:
        """Нормализует один аналитический путь вместе со служебными точками кривых."""

        segments = []
        for segment in path["segments"]:
            normalized: dict[str, Any] = {"type": segment["type"]}
            for key in ("end", "center", "control1", "control2"):
                if key in segment:
                    normalized[key] = _normalized_point(
                        segment[key], rotation, minimum_x, minimum_y, scale
                    )
            if "clockwise" in segment:
                normalized["clockwise"] = segment["clockwise"]
            segments.append(normalized)
        return {
            "start": _normalized_point(path["start"], rotation, minimum_x, minimum_y, scale),
            "segments": segments,
        }

    return {
        "outer": normalize_path(part["outer"]),
        "holes": [normalize_path(path) for path in part["holes"]],
    }


def polygon_family_hash(problem: Mapping[str, Any]) -> str:
    """Вычисляет хеш набора фигур, неизменный к повороту и равномерному масштабу."""

    signatures = []
    for part in problem["parts"]:
        rotations = [canonical_json(_normalized_part(part, angle)) for angle in (0, 90, 180, 270)]
        signatures.append(min(rotations))
    payload = canonical_json({"parts": sorted(signatures)})
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def problem_features(problem: Mapping[str, Any]) -> list[str]:
    """Восстанавливает проверяемые классы геометрии из `polygon_problem` v1."""

    result: set[str] = set()
    for part in problem["parts"]:
        paths = [part["outer"], *part["holes"]]
        segments = [segment for path in paths for segment in path["segments"]]
        if any(segment["type"] == "arc" for segment in segments):
            result.add("arc")
        if any(segment["type"] == "cubic_bezier" for segment in segments):
            result.add("bezier")
        if part["holes"]:
            result.add("hole")

        points = [part["outer"]["start"], *(segment["end"] for segment in part["outer"]["segments"])]
        xs = [float(point["x"]) for point in points]
        ys = [float(point["y"]) for point in points]
        width, height = max(xs) - min(xs), max(ys) - min(ys)
        if max(width, height) >= 4.0 * max(min(width, height), 1e-9):
            result.add("thin")
        if all(segment["type"] == "line" for segment in part["outer"]["segments"]):
            vertices = [(float(point["x"]), float(point["y"])) for point in points[:-1]]
            crosses = []
            for index in range(len(vertices)):
                previous = vertices[index - 1]
                current = vertices[index]
                following = vertices[(index + 1) % len(vertices)]
                cross = ((current[0] - previous[0]) * (following[1] - current[1])
                         - (current[1] - previous[1]) * (following[0] - current[0]))
                if abs(cross) > 1e-9:
                    crosses.append(math.copysign(1.0, cross))
            result.add("concave" if len(set(crosses)) > 1 else "convex")
            center_x, center_y = (min(xs) + max(xs)) / 2.0, (min(ys) + max(ys)) / 2.0
            centered = {(round(x - center_x, 6), round(y - center_y, 6)) for x, y in vertices}
            if centered and all((-x, -y) in centered for x, y in centered):
                result.add("symmetric")
    return sorted(result)


def validate_problem_profile(problem: Mapping[str, Any], tier: str) -> None:
    """Проверяет лист, количества и максимальный габарит по заявленному профилю."""

    if tier not in POLYGON_PROFILES:
        raise ValueError(f"неизвестный профиль задачи: {tier}")
    profile = POLYGON_PROFILES[tier]
    width, height = float(problem["sheet"]["width"]), float(problem["sheet"]["height"])
    if not profile["sheetWidth"][0] <= width <= profile["sheetWidth"][1]:
        raise ValueError(f"ширина листа не входит в профиль {tier}")
    if not profile["sheetHeight"][0] <= height <= profile["sheetHeight"][1]:
        raise ValueError(f"высота листа не входит в профиль {tier}")
    if not profile["partTypes"][0] <= len(problem["parts"]) <= profile["partTypes"][1]:
        raise ValueError(f"число типов деталей не входит в профиль {tier}")
    instances = sum(int(part["quantity"]) for part in problem["parts"])
    if not profile["instances"][0] <= instances <= profile["instances"][1]:
        raise ValueError(f"число экземпляров не входит в профиль {tier}")
    for part in problem["parts"]:
        points = list(path_points(part["outer"]))
        extent_x = max(float(point["x"]) for point in points) - min(float(point["x"]) for point in points)
        extent_y = max(float(point["y"]) for point in points) - min(float(point["y"]) for point in points)
        minimum, maximum = profile["partExtent"]
        if not minimum <= max(extent_x, extent_y) <= maximum:
            raise ValueError(f"габарит детали не входит в профиль {tier}: {part['id']}")
