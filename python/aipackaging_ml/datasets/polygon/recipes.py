"""Аналитические рецепты полигональных деталей для пробных и рабочих наборов данных."""

from __future__ import annotations

from typing import Any, Mapping

POLYGON_FEATURES = ("convex", "concave", "arc", "bezier", "hole", "thin", "symmetric")


def point(x: float, y: float) -> dict[str, float]:
    """Создаёт каноническую миллиметровую точку с микронной точностью."""

    return {"x": round(x, 3), "y": round(y, 3)}


def line(x: float, y: float) -> dict[str, Any]:
    """Создаёт JSON-сегмент прямой до заданной точки."""

    return {"type": "line", "end": point(x, y)}


def scaled_shape(kind: str, width: float, height: float) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Строит аналитический контур семейства внутри ограничивающего прямоугольника."""

    holes: list[dict[str, Any]] = []
    if kind == "convex":
        outer = {"start": point(0, height * 0.25), "segments": [
            line(width * 0.2, 0), line(width * 0.82, height * 0.08), line(width, height * 0.68),
            line(width * 0.65, height), line(width * 0.08, height * 0.82), line(0, height * 0.25),
        ]}
    elif kind == "concave":
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height * 0.42), line(width * 0.48, height * 0.42),
            line(width * 0.48, height), line(0, height), line(0, 0),
        ]}
    elif kind == "arc":
        # Центр и концы выводятся из одного микронно-кратного радиуса, чтобы
        # независимое округление не нарушило строгую проверку окружности.
        radius = round(min(width, height) / 2.0, 3)
        diameter = round(2.0 * radius, 3)
        outer = {"start": point(0, radius), "segments": [
            {"type": "arc", "end": point(diameter, radius), "center": point(radius, radius), "clockwise": False},
            {"type": "arc", "end": point(0, radius), "center": point(radius, radius), "clockwise": False},
        ]}
    elif kind == "bezier":
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height * 0.65),
            {"type": "cubic_bezier", "end": point(0, height * 0.65),
             "control1": point(width * 0.78, height), "control2": point(width * 0.22, height)},
            line(0, 0),
        ]}
    elif kind == "hole":
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height), line(0, height), line(0, 0),
        ]}
        holes = [{"start": point(width * 0.3, height * 0.3), "segments": [
            line(width * 0.7, height * 0.3), line(width * 0.7, height * 0.7),
            line(width * 0.3, height * 0.7), line(width * 0.3, height * 0.3),
        ]}]
    elif kind == "thin":
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height), line(0, height), line(0, 0),
        ]}
    elif kind == "symmetric":
        # Центрально-симметричный крест образует отдельный класс вогнутых деталей.
        x1, x2 = width / 3.0, 2.0 * width / 3.0
        y1, y2 = height / 3.0, 2.0 * height / 3.0
        outer = {"start": point(x1, 0), "segments": [
            line(x2, 0), line(x2, y1), line(width, y1), line(width, y2), line(x2, y2),
            line(x2, height), line(x1, height), line(x1, y2), line(0, y2), line(0, y1),
            line(x1, y1), line(x1, 0),
        ]}
    else:
        raise ValueError(f"неизвестный вид полигональной фигуры: {kind}")
    return outer, holes


def scale_path(path: Mapping[str, Any], scale: float) -> dict[str, Any]:
    """Равномерно масштабирует аналитический путь, сохраняя типы сегментов."""

    result: dict[str, Any] = {
        "start": point(float(path["start"]["x"]) * scale, float(path["start"]["y"]) * scale),
        "segments": [],
    }
    for segment in path["segments"]:
        scaled: dict[str, Any] = {"type": segment["type"]}
        for key in ("end", "center", "control1", "control2"):
            if key in segment:
                scaled[key] = point(float(segment[key]["x"]) * scale, float(segment[key]["y"]) * scale)
        if "clockwise" in segment:
            scaled["clockwise"] = segment["clockwise"]
        result["segments"].append(scaled)
    return result


def shape(kind: int, scale: float) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Сохраняет прежний рецепт пробной фигуры M4 по числовому индексу."""

    width = 16.0 + scale
    height = 12.0 + scale / 2.0
    holes: list[dict[str, Any]] = []
    if kind == 0:
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height), line(0, height), line(0, 0),
        ]}
    elif kind == 1:
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height / 2), line(width / 2, height / 2),
            line(width / 2, height), line(0, height), line(0, 0),
        ]}
    elif kind == 2:
        radius = round(width / 2, 3)
        width = 2 * radius
        outer = {"start": point(0, radius), "segments": [
            {"type": "arc", "end": point(width, radius), "center": point(radius, radius), "clockwise": False},
            {"type": "arc", "end": point(0, radius), "center": point(radius, radius), "clockwise": False},
        ]}
    elif kind == 3:
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height),
            {"type": "cubic_bezier", "end": point(0, height),
             "control1": point(width * 0.75, height * 1.45),
             "control2": point(width * 0.25, height * 1.45)},
            line(0, 0),
        ]}
    else:
        outer = {"start": point(0, 0), "segments": [
            line(width, 0), line(width, height), line(0, height), line(0, 0),
        ]}
        holes = [{"start": point(width * 0.3, height * 0.3), "segments": [
            line(width * 0.7, height * 0.3), line(width * 0.7, height * 0.7),
            line(width * 0.3, height * 0.7), line(width * 0.3, height * 0.3),
        ]}]
    return outer, holes
