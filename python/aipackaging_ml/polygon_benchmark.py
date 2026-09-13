"""Совместимый публичный фасад сравнения полигональных базовых алгоритмов."""

from .datasets.polygon.benchmark import benchmark_polygon_baselines, verify_polygon_benchmark

__all__ = ["benchmark_polygon_baselines", "verify_polygon_benchmark"]
