"""Совместимый публичный фасад полигонального dataset pipeline v1."""

from .datasets.polygon.generation import generate_polygon_problem
from .datasets.polygon.pipeline import POLYGON_SPLITS, generate_polygon_dataset
from .datasets.polygon.rollout import POLYGON_SOLVERS
from .datasets.polygon.verification import verify_polygon_dataset

__all__ = [
    "POLYGON_SOLVERS",
    "POLYGON_SPLITS",
    "generate_polygon_problem",
    "generate_polygon_dataset",
    "verify_polygon_dataset",
]
