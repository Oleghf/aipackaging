"""Переносимый выбор полигональных действий при развёртывании модели."""

from __future__ import annotations

import math
from collections.abc import Sequence

_MASK64 = (1 << 64) - 1
_INVERSE_53 = 1.0 / float(1 << 53)


class SplitMix64:
    """Формирует одинаковую последовательность случайных чисел в Python и C++."""

    def __init__(self, seed: int) -> None:
        """Сохраняет младшие 64 бита начального значения генератора."""

        self._state = int(seed) & _MASK64

    def next_u64(self) -> int:
        """Возвращает следующее беззнаковое 64-битное значение SplitMix64."""

        self._state = (self._state + 0x9E3779B97F4A7C15) & _MASK64
        value = self._state
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & _MASK64
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & _MASK64
        return (value ^ (value >> 31)) & _MASK64

    def uniform(self) -> float:
        """Возвращает число из полуинтервала [0, 1) с 53 значащими битами."""

        return float(self.next_u64() >> 11) * _INVERSE_53


def rollout_seed(master_seed: int, rollout_index: int) -> int:
    """Получает независимое начальное значение одного нейросетевого прогона."""

    if rollout_index < 0:
        raise ValueError("индекс нейросетевого прогона не может быть отрицательным")
    return SplitMix64((int(master_seed) + int(rollout_index)) & _MASK64).next_u64()


def select_logit(logits: Sequence[float], legal: Sequence[bool], generator: SplitMix64 | None) -> int:
    """Выбирает допустимый индекс жадно либо по стабилизированному распределению."""

    if len(logits) != len(legal):
        raise ValueError("оценки и маска допустимости имеют разные размеры")
    indices = [index for index, allowed in enumerate(legal) if allowed]
    if not indices:
        raise ValueError("уровень политики не содержит допустимых вариантов")
    values = [float(logits[index]) for index in indices]
    if any(not math.isfinite(value) for value in values):
        raise ValueError("модель вернула нечисловую или бесконечную оценку")
    if generator is None:
        # `max` сохраняет первый элемент при равенстве, поэтому правило минимального
        # индекса не зависит от реализации сортировки.
        return max(zip(values, indices, strict=True), key=lambda item: item[0])[1]
    maximum = max(values)
    weights = [math.exp(value - maximum) for value in values]
    # Обычное последовательное суммирование повторяет `std::accumulate` в C++.
    total = sum(weights)
    threshold = generator.uniform() * total
    cumulative = 0.0
    for index, weight in zip(indices, weights, strict=True):
        cumulative += weight
        if threshold < cumulative:
            return index
    # Последний вариант закрывает только погрешность суммирования на правой границе.
    return indices[-1]
