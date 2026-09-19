"""Оболочка нативной клеточной среды с интерфейсом Gymnasium без зависимости от него."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping

from . import _aipackaging_solver as _native
from .datasets.serialization import canonical_json


class GridNestingEnv:
    """Предоставляет один детерминированный эпизод клеточного раскроя v1."""

    def __init__(self, native_environment: _native.GridLearningEnvironment) -> None:
        """Сохраняет уже проверенный нативный эпизод; используйте фабрики `from_*`."""

        self._native = native_environment
        self._static_observation = native_environment.static_observation()

    @classmethod
    def from_file(
        cls, path: str | Path, *, max_sheet_area: int = 4096, max_actions: int = 1_000_000
    ) -> "GridNestingEnv":
        """Загружает строгий `grid_problem` v1 из файла JSON в кодировке UTF-8."""

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

        # Среда детерминирована, поэтому начальное значение принято для совместимости с API Gymnasium
        # и намеренно не меняет порядок постоянного каталога действий.
        observation = self._native.reset()
        return observation, {
            "seed": seed,
            "problemId": self.problem_id,
            "rank": self.rank,
            "rankUpperBound": self.rank_upper_bound,
        }

    def reset_compact(self, seed: int | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        """Сбрасывает эпизод без повторного копирования постоянного каталога действий."""

        observation = self._native.reset_compact()
        return observation, {
            "seed": seed,
            "problemId": self.problem_id,
            "rank": self.rank,
            "rankUpperBound": self.rank_upper_bound,
        }

    def step(self, action_index: int) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        """Применяет допустимый индекс и возвращает результат перехода в стиле Gymnasium."""

        return self._native.step(action_index)

    def step_compact(self, action_index: int) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        """Применяет индекс и возвращает только динамические массивы наблюдения v1."""

        return self._native.step_compact(action_index)

    def observation(self) -> dict[str, Any]:
        """Возвращает независимый снимок текущего наблюдения v1 только для чтения."""

        return self._native.observation()

    def static_observation(self) -> dict[str, Any]:
        """Возвращает кэшированную неизменную часть наблюдения текущей задачи."""

        return dict(self._static_observation)

    def dynamic_observation(self) -> dict[str, Any]:
        """Возвращает независимый снимок изменяемой части наблюдения текущего состояния."""

        return self._native.dynamic_observation()

    def snapshot_solution(
        self, provenance: Mapping[str, Any], *, incomplete_status: str = "budget_exhausted"
    ) -> dict[str, Any]:
        """Формирует `grid_solution` v2 из применённых действий и заданного происхождения."""

        return json.loads(self._native.snapshot_solution(dict(provenance), incomplete_status))

    def action(self, index: int) -> dict[str, Any]:
        """Возвращает аудируемое размещение по стабильному индексу каталога."""

        return self._native.action(index)

    def find_action(self, action: Mapping[str, Any]) -> int:
        """Находит стабильный индекс сериализованного размещения для повторного проигрывания."""

        return self._native.find_action(dict(action))

    @property
    def action_count(self) -> int:
        """Возвращает постоянный размер пространства действий эпизода."""

        return self._native.action_count

    @property
    def is_complete(self) -> bool:
        """Сообщает, размещены ли все экземпляры деталей."""

        return self._native.is_complete

    @property
    def is_terminal(self) -> bool:
        """Сообщает, закончен ли эпизод полным решением либо тупиковым состоянием."""

        return self._native.is_terminal

    @property
    def is_dead_end(self) -> bool:
        """Сообщает, закончился ли неполный эпизод без допустимых действий."""

        return self._native.is_dead_end

    @property
    def rank(self) -> int:
        """Возвращает текущий точный смешанный ранг вознаграждения v1."""

        return self._native.rank

    @property
    def rank_upper_bound(self) -> int:
        """Возвращает знаменатель нормализованной награды эпизода."""

        return self._native.rank_upper_bound

    @property
    def problem_id(self) -> str:
        """Возвращает идентификатор исходной задачи."""

        return self._native.problem_id


class PolygonNestingEnv:
    """Предоставляет один эпизод полигонального раскроя с динамическими действиями."""

    def __init__(self, native_environment: _native.PolygonLearningEnvironment) -> None:
        """Сохраняет уже проверенную нативную среду; используйте фабрики `from_*`."""

        self._native = native_environment

    @classmethod
    def from_file(cls, path: str | Path, *, reward_version: int = 1, catalog_version: int = 2) -> "PolygonNestingEnv":
        """Загружает `polygon_problem` v1; по умолчанию выбирает исправленный каталог v2."""

        return cls(_native.create_polygon_environment(Path(path).read_text(encoding="utf-8"), reward_version, catalog_version))

    @classmethod
    def from_dict(cls, problem: Mapping[str, Any], *, reward_version: int = 1, catalog_version: int = 2) -> "PolygonNestingEnv":
        """Создаёт эпизод из `polygon_problem` v1 с выбранной версией каталога."""

        return cls(_native.create_polygon_environment(canonical_json(problem), reward_version, catalog_version))

    def reset(self, seed: int | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        """Сбрасывает эпизод и сообщает текущий размер пространства действий."""

        observation = self._native.reset()
        return observation, {"seed": seed, "problemId": self.problem_id, "actionCount": self.action_count}

    def observation(self) -> dict[str, Any]:
        """Возвращает независимый снимок базового наблюдения только для чтения."""

        return self._native.observation()

    def static_observation(self) -> dict[str, Any]:
        """Возвращает неизменные растры ориентаций и признаки экземпляров."""

        return self._native.static_observation()

    def dynamic_observation(self) -> dict[str, Any]:
        """Возвращает изменяемые растры, признаки состояния и маску допустимых пар."""

        return self._native.dynamic_observation()

    def reset_compact(self, seed: int | None = None) -> tuple[dict[str, Any], dict[str, Any]]:
        """Сбрасывает эпизод без повторного копирования статического наблюдения."""

        observation = self._native.reset_compact()
        return observation, {"seed": seed, "problemId": self.problem_id, "actionCount": self.action_count}

    def placement_observation(self, instance_index: int, rotation_degrees: int) -> dict[str, Any]:
        """Возвращает четыре канала и кандидаты выбранной иерархической пары."""

        return self._native.placement_observation(instance_index, rotation_degrees)

    def actions(self) -> list[dict[str, Any]]:
        """Возвращает снимок текущего динамического каталога действий."""

        return list(self._native.actions())

    def action(self, index: int) -> dict[str, Any]:
        """Возвращает действие текущего каталога с обычной проверкой индекса Python."""

        actions = self.actions()
        return actions[index]

    def find_action(self, action: Mapping[str, Any]) -> int:
        """Находит действие в текущем каталоге для точного повторного проигрывания."""

        target = dict(action)
        for index, candidate in enumerate(self.actions()):
            if candidate == target:
                return index
        raise ValueError("действие отсутствует в текущем полигональном каталоге")

    def step(self, action_index: int) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        """Применяет индекс текущего каталога и возвращает переход в стиле Gymnasium."""

        return self._native.step(action_index)

    def step_compact(self, action_index: int) -> tuple[dict[str, Any], float, bool, bool, dict[str, Any]]:
        """Применяет действие и возвращает только изменяемую часть наблюдения."""

        return self._native.step_compact(action_index)

    def snapshot_solution(self, provenance: Mapping[str, Any]) -> dict[str, Any]:
        """Возвращает независимо проверенное `polygon_solution` v1 текущего состояния."""

        return json.loads(self._native.snapshot_solution(dict(provenance)))

    @property
    def action_count(self) -> int:
        """Возвращает размер каталога, действительного только на текущем шаге."""

        return len(self._native.actions())

    @property
    def is_complete(self) -> bool:
        """Сообщает, размещены ли все обязательные экземпляры."""

        return self._native.is_complete

    @property
    def is_terminal(self) -> bool:
        """Сообщает о полноте либо отсутствии следующих допустимых действий."""

        return self._native.is_terminal

    @property
    def problem_id(self) -> str:
        """Возвращает идентификатор исходной полигональной задачи."""

        return self._native.problem_id
