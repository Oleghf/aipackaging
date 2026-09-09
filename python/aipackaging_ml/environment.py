"""Gym-like оболочка над нативной клеточной средой без зависимости от Gymnasium."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from . import _aipackaging_solver as _native


def canonical_json(value: Mapping[str, Any]) -> str:
    """Сериализует словарь стабильным компактным JSON для нативного strict parser."""

    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


class GridNestingEnv:
    """Предоставляет один детерминированный эпизод клеточного раскроя v1."""

    def __init__(self, native_environment: _native.GridLearningEnvironment) -> None:
        """Сохраняет уже проверенный нативный эпизод; используйте фабрики ``from_*``."""

        self._native = native_environment

    @classmethod
    def from_file(
        cls, path: str | Path, *, max_sheet_area: int = 4096, max_actions: int = 1_000_000
    ) -> "GridNestingEnv":
        """Загружает строгий ``grid_problem`` v1 из UTF-8 JSON-файла."""

        text = Path(path).read_text(encoding="utf-8")
        return cls(_native.create_environment(text, max_sheet_area, max_actions))

    @classmethod
    def from_dict(
        cls, problem: Mapping[str, Any], *, max_sheet_area: int = 4096, max_actions: int = 1_000_000
    ) -> "GridNestingEnv":
        """Создаёт эпизод из JSON-совместимого словаря задачи."""

        return cls(_native.create_environment(canonical_json(problem), max_sheet_area, max_actions))

    def reset(self, seed: int | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        """Сбрасывает эпизод и возвращает начальное наблюдение с диагностикой."""

        # Среда детерминирована, поэтому seed принят для совместимости gym-like API
        # и намеренно не меняет порядок постоянного каталога действий.
        observation = self._native.reset()
        return observation, {
            "seed": seed,
            "problemId": self.problem_id,
            "rank": self.rank,
            "rankUpperBound": self.rank_upper_bound,
        }

    def step(self, action_index: int) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        """Применяет допустимый индекс и возвращает gym-like результат перехода."""

        return self._native.step(action_index)

    def observation(self) -> dict[str, Any]:
        """Возвращает независимый read-only снимок текущего observation v1."""

        return self._native.observation()

    def action(self, index: int) -> dict[str, Any]:
        """Возвращает аудируемое размещение по стабильному индексу каталога."""

        return self._native.action(index)

    def find_action(self, action: Mapping[str, Any]) -> int:
        """Находит стабильный индекс сериализованного размещения для replay."""

        return self._native.find_action(dict(action))

    @property
    def action_count(self) -> int:
        """Возвращает постоянный размер action space эпизода."""

        return self._native.action_count

    @property
    def is_complete(self) -> bool:
        """Сообщает, размещены ли все экземпляры деталей."""

        return self._native.is_complete

    @property
    def is_terminal(self) -> bool:
        """Сообщает, закончен ли эпизод полным решением либо dead-end."""

        return self._native.is_terminal

    @property
    def is_dead_end(self) -> bool:
        """Сообщает, закончился ли неполный эпизод без допустимых действий."""

        return self._native.is_dead_end

    @property
    def rank(self) -> int:
        """Возвращает текущий точный смешанный ранг reward v1."""

        return self._native.rank

    @property
    def rank_upper_bound(self) -> int:
        """Возвращает знаменатель нормализованной награды эпизода."""

        return self._native.rank_upper_bound

    @property
    def problem_id(self) -> str:
        """Возвращает идентификатор исходной задачи."""

        return self._native.problem_id
