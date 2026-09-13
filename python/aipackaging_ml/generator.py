"""Совместимый публичный фасад детерминированного клеточного генератора."""

from .datasets.grid.generation import (
    PROFILES,
    GenerationProfile,
    derive_seed,
    family_hash,
    generate_problem,
    generate_unique_problem,
    grow_polyomino,
    unique_orientations,
)

__all__ = [
    "PROFILES",
    "GenerationProfile",
    "derive_seed",
    "family_hash",
    "generate_problem",
    "generate_unique_problem",
    "grow_polyomino",
    "unique_orientations",
]
