"""Публичный API Python среды и политики AIPackaging с отложенным импортом нативного модуля."""

from __future__ import annotations

from typing import Any

__all__ = ["GridNestingEnv", "PolygonNestingEnv"]
__version__ = "0.9.0"


def __getattr__(name: str) -> Any:
    """Загружает нативную среду только при фактическом обращении к её классу."""

    if name in {"GridNestingEnv", "PolygonNestingEnv"}:
        from .environment import GridNestingEnv, PolygonNestingEnv

        return {"GridNestingEnv": GridNestingEnv, "PolygonNestingEnv": PolygonNestingEnv}[name]
    raise AttributeError(name)
