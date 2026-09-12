"""Аналитические рецепты фигур полигонального smoke-датасета v1."""

from __future__ import annotations

from typing import Any


def point(x: float, y: float) -> dict[str, float]:
    """Создаёт канонический словарь миллиметровой точки."""

    return {"x": round(x, 3), "y": round(y, 3)}


def line(x: float, y: float) -> dict[str, Any]:
    """Создаёт JSON-сегмент прямой до заданной точки."""

    return {"type": "line", "end": point(x, y)}


def shape(kind: int, scale: float) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Возвращает выпуклый, вогнутый, дуговой, Bézier или дырчатый контур."""

    width = 16.0 + scale
    height = 12.0 + scale / 2.0
    holes: list[dict[str, Any]] = []
    if kind == 0:
        outer = {
            "start": point(0, 0),
            "segments": [line(width, 0), line(width, height), line(0, height), line(0, 0)],
        }
    elif kind == 1:
        outer = {
            "start": point(0, 0),
            "segments": [
                line(width, 0),
                line(width, height / 2),
                line(width / 2, height / 2),
                line(width / 2, height),
                line(0, height),
                line(0, 0),
            ],
        }
    elif kind == 2:
        radius = round(width / 2, 3)
        width = 2 * radius
        outer = {
            "start": point(0, radius),
            "segments": [
                {
                    "type": "arc",
                    "end": point(width, radius),
                    "center": point(radius, radius),
                    "clockwise": False,
                },
                {
                    "type": "arc",
                    "end": point(0, radius),
                    "center": point(radius, radius),
                    "clockwise": False,
                },
            ],
        }
    elif kind == 3:
        outer = {
            "start": point(0, 0),
            "segments": [
                line(width, 0),
                line(width, height),
                {
                    "type": "cubic_bezier",
                    "end": point(0, height),
                    "control1": point(width * 0.75, height * 1.45),
                    "control2": point(width * 0.25, height * 1.45),
                },
                line(0, 0),
            ],
        }
    else:
        outer = {
            "start": point(0, 0),
            "segments": [line(width, 0), line(width, height), line(0, height), line(0, 0)],
        }
        holes = [
            {
                "start": point(width * 0.3, height * 0.3),
                "segments": [
                    line(width * 0.7, height * 0.3),
                    line(width * 0.7, height * 0.7),
                    line(width * 0.3, height * 0.7),
                    line(width * 0.3, height * 0.3),
                ],
            }
        ]
    return outer, holes
