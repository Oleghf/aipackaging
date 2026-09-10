"""Публичный Python API среды и политики AIPackaging с ленивым native import."""

from __future__ import annotations

from typing import Any

__all__ = ["GridNestingEnv"]
__version__ = "0.3.0"


def __getattr__(name: str) -> Any:
    """Загружает нативную среду только при фактическом обращении к её классу."""

    if name == "GridNestingEnv":
        from .environment import GridNestingEnv

        return GridNestingEnv
    raise AttributeError(name)
