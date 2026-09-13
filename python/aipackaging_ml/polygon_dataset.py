"""Совместимый публичный фасад конвейера полигонального набора данных v1/v2."""

from .datasets.polygon.family import polygon_family_hash
from .datasets.polygon.generation import POLYGON_PROFILES, POLYGON_TIERS, generate_polygon_problem
from .datasets.polygon.pipeline import POLYGON_SMOKE_SPLITS, POLYGON_SPLITS, generate_polygon_dataset
from .datasets.polygon.rollout import POLYGON_BUDGETS, POLYGON_SMOKE_BUDGETS, POLYGON_SOLVERS
from .datasets.polygon.verification import load_polygon_dataset_records, verify_polygon_dataset

__all__ = [
    "POLYGON_BUDGETS",
    "POLYGON_PROFILES",
    "POLYGON_SMOKE_BUDGETS",
    "POLYGON_SMOKE_SPLITS",
    "POLYGON_SOLVERS",
    "POLYGON_SPLITS",
    "POLYGON_TIERS",
    "generate_polygon_problem",
    "generate_polygon_dataset",
    "load_polygon_dataset_records",
    "polygon_family_hash",
    "verify_polygon_dataset",
]
