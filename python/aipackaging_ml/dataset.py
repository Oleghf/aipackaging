"""Совместимый публичный фасад клеточного конвейера набора данных v1."""

from .datasets.grid.pipeline import DEFAULT_SPLITS, generate_dataset
from .datasets.grid.rollout import SOLVERS
from .datasets.grid.verification import verify_dataset

__all__ = ["DEFAULT_SPLITS", "SOLVERS", "generate_dataset", "verify_dataset"]
