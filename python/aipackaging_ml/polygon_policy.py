"""Безопасный выбор действий и запуск полигональной нейросетевой политики."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Mapping

import numpy as np
import torch
from torch import Tensor

from . import __version__, _aipackaging_solver as _native
from .datasets.serialization import canonical_json
from .environment import PolygonNestingEnv
from .polygon_model import EncodedPolygonState, HierarchicalPolygonPolicyV1


@dataclass(frozen=True)
class PolygonPolicyDecision:
    """Содержит выбранное действие и дифференцируемые значения политики."""

    action_index: int
    instance_position: int
    rotation_slot: int
    position_index: int
    log_probability: Tensor
    entropy: Tensor
    value: Tensor


@dataclass(frozen=True)
class PolygonPairDecision:
    """Содержит первый и второй уровни выбора до запроса геометрии позиции."""

    instance_position: int
    rotation_slot: int
    log_probability: Tensor
    entropy: Tensor
    encoded: EncodedPolygonState


def _tensor(value: np.ndarray, device: torch.device, *, dtype: torch.dtype) -> Tensor:
    """Копирует неизменяемый массив NumPy в тензор требуемого типа."""

    return torch.as_tensor(np.array(value, copy=True), dtype=dtype, device=device)


def encode_polygon_observation(
    model: HierarchicalPolygonPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    device: torch.device,
) -> EncodedPolygonState:
    """Преобразует компактное наблюдение и запускает кодировщик политики."""

    sheet = np.stack((dynamic["occupied"], dynamic["clearance"]), axis=0)
    return model.encode(
        _tensor(sheet, device, dtype=torch.float32),
        _tensor(np.asarray(fixed["part_masks"]), device, dtype=torch.float32),
        _tensor(np.asarray(fixed["part_features"]), device, dtype=torch.float32),
        _tensor(np.asarray(dynamic["objective"]), device, dtype=torch.float32),
    )


def _masked_choice(logits: Tensor, legal: Tensor, generator: torch.Generator | None) -> tuple[int, Tensor, Tensor]:
    """Выбирает только допустимое значение и стабильно разрешает равенство оценок."""

    indices = torch.nonzero(legal, as_tuple=False).flatten()
    if indices.numel() == 0:
        raise RuntimeError("уровень полигональной политики не имеет допустимых вариантов")
    legal_logits = logits.index_select(0, indices)
    probabilities = torch.softmax(legal_logits, dim=0)
    if generator is None:
        local = torch.argmax(legal_logits)
    else:
        local = torch.multinomial(probabilities, 1, generator=generator).squeeze(0)
    log_probabilities = torch.log_softmax(legal_logits, dim=0)
    entropy = -(probabilities * log_probabilities).sum()
    return int(indices[local].item()), log_probabilities[local], entropy


def _instance_position(fixed: Mapping[str, Any], action: Mapping[str, Any]) -> int:
    """Находит глобальную позицию экземпляра по аудируемым полям действия."""

    indices = np.asarray(fixed["instance_indices"])
    for position, (part_id, instance_index) in enumerate(zip(fixed["instance_part_ids"], indices, strict=True)):
        if part_id == action["partId"] and int(instance_index) == action["instanceIndex"]:
            return position
    raise ValueError("действие ссылается на неизвестный экземпляр полигональной детали")


def _position_logits(
    model: HierarchicalPolygonPolicyV1,
    encoded: EncodedPolygonState,
    environment: PolygonNestingEnv,
    instance: int,
    rotation: int,
    device: torch.device,
) -> tuple[Tensor, Mapping[str, Any]]:
    """Получает условное наблюдение пары и вычисляет оценки её кандидатов."""

    placement = environment.placement_observation(instance, rotation * 90)
    candidates = np.asarray(placement["candidate_features"])
    if candidates.shape[0] == 0:
        raise RuntimeError("выбранная допустимая пара не содержит позиций")
    logits = model.positions(
        encoded.state_embedding,
        encoded.orientation_embeddings[instance, rotation],
        _tensor(np.asarray(placement["raster"]), device, dtype=torch.float32),
        _tensor(candidates, device, dtype=torch.float32),
    )
    return logits, placement


def evaluate_polygon_action(
    model: HierarchicalPolygonPolicyV1,
    environment: PolygonNestingEnv,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    action_index: int,
    device: torch.device,
) -> PolygonPolicyDecision:
    """Вычисляет факторизованную вероятность известного допустимого действия."""

    actions = environment.actions()
    if action_index < 0 or action_index >= len(actions):
        raise IndexError("индекс полигонального действия находится вне текущего каталога")
    target = actions[action_index]
    instance = _instance_position(fixed, target)
    rotation = int(target["rotationDegrees"]) // 90
    pair_mask = np.asarray(dynamic["pair_mask"], dtype=np.bool_)
    if not pair_mask[instance, rotation]:
        raise ValueError("действие запрещено маской допустимых пар")
    placement = environment.placement_observation(instance, rotation * 90)
    position = next((index for index, value in enumerate(placement["actions"]) if value == target), -1)
    if position < 0:
        raise ValueError("действие отсутствует в условном каталоге выбранной пары")
    result = evaluate_polygon_components(model, fixed, dynamic, placement, instance, rotation, position, device)
    return PolygonPolicyDecision(action_index, instance, rotation, position, result.log_probability, result.entropy, result.value)


def evaluate_polygon_components(
    model: HierarchicalPolygonPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    placement: Mapping[str, Any],
    instance: int,
    rotation: int,
    position: int,
    device: torch.device,
) -> PolygonPolicyDecision:
    """Повторно оценивает сохранённый иерархический выбор без обращения к среде."""

    pair_mask = np.asarray(dynamic["pair_mask"], dtype=np.bool_)
    if instance < 0 or instance >= pair_mask.shape[0] or rotation < 0 or rotation >= 4 or not pair_mask[instance, rotation]:
        raise ValueError("сохранённая пара полигонального действия запрещена")
    encoded = encode_polygon_observation(model, fixed, dynamic, device)
    legal_instances = np.flatnonzero(pair_mask.any(axis=1))
    instance_local = int(np.flatnonzero(legal_instances == instance)[0])
    instance_values = encoded.instance_logits.index_select(0, _tensor(legal_instances, device, dtype=torch.int64))
    instance_log = torch.log_softmax(instance_values, dim=0)
    instance_prob = torch.softmax(instance_values, dim=0)
    legal_rotations = np.flatnonzero(pair_mask[instance])
    rotation_local = int(np.flatnonzero(legal_rotations == rotation)[0])
    rotation_values = encoded.rotation_logits[instance].index_select(0, _tensor(legal_rotations, device, dtype=torch.int64))
    rotation_log = torch.log_softmax(rotation_values, dim=0)
    rotation_prob = torch.softmax(rotation_values, dim=0)
    position_values = model.positions(
        encoded.state_embedding,
        encoded.orientation_embeddings[instance, rotation],
        _tensor(np.asarray(placement["raster"]), device, dtype=torch.float32),
        _tensor(np.asarray(placement["candidate_features"]), device, dtype=torch.float32),
    )
    if position < 0 or position >= position_values.shape[0]:
        raise IndexError("индекс позиции находится вне условного каталога")
    position_log = torch.log_softmax(position_values, dim=0)
    position_prob = torch.softmax(position_values, dim=0)
    entropy = (
        -(instance_prob * instance_log).sum()
        -(rotation_prob * rotation_log).sum()
        -(position_prob * position_log).sum()
    )
    return PolygonPolicyDecision(
        -1,
        instance,
        rotation,
        position,
        instance_log[instance_local] + rotation_log[rotation_local] + position_log[position],
        entropy,
        encoded.value,
    )


def select_polygon_action(
    model: HierarchicalPolygonPolicyV1,
    environment: PolygonNestingEnv,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    device: torch.device,
    generator: torch.Generator | None = None,
) -> PolygonPolicyDecision:
    """Последовательно выбирает допустимые экземпляр, поворот и позицию."""

    pair = select_polygon_pair(model, fixed, dynamic, device, generator)
    position_values, placement = _position_logits(
        model, pair.encoded, environment, pair.instance_position, pair.rotation_slot, device
    )
    position, position_log, position_entropy = _masked_choice(
        position_values, torch.ones_like(position_values, dtype=torch.bool), generator
    )
    action_index = environment.find_action(placement["actions"][position])
    return PolygonPolicyDecision(
        action_index,
        pair.instance_position,
        pair.rotation_slot,
        position,
        pair.log_probability + position_log,
        pair.entropy + position_entropy,
        pair.encoded.value,
    )


def select_polygon_pair(
    model: HierarchicalPolygonPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    device: torch.device,
    generator: torch.Generator | None = None,
) -> PolygonPairDecision:
    """Выбирает допустимые экземпляр и поворот без обращения к геометрии позиций."""

    pair_mask = np.asarray(dynamic["pair_mask"], dtype=np.bool_)
    if not pair_mask.any():
        raise RuntimeError("нельзя выбирать действие в конечном полигональном состоянии")
    encoded = encode_polygon_observation(model, fixed, dynamic, device)
    instance, instance_log, instance_entropy = _masked_choice(
        encoded.instance_logits, _tensor(pair_mask.any(axis=1), device, dtype=torch.bool), generator
    )
    rotation, rotation_log, rotation_entropy = _masked_choice(
        encoded.rotation_logits[instance], _tensor(pair_mask[instance], device, dtype=torch.bool), generator
    )
    return PolygonPairDecision(
        instance,
        rotation,
        instance_log + rotation_log,
        instance_entropy + rotation_entropy,
        encoded,
    )


def select_polygon_position(
    model: HierarchicalPolygonPolicyV1,
    pair: PolygonPairDecision,
    placement: Mapping[str, Any],
    device: torch.device,
    generator: torch.Generator | None = None,
) -> PolygonPolicyDecision:
    """Выбирает позицию из условного каталога ранее выбранной пары."""

    candidates = np.asarray(placement["candidate_features"])
    if candidates.shape[0] == 0:
        raise RuntimeError("выбранная допустимая пара не содержит позиций")
    values = model.positions(
        pair.encoded.state_embedding,
        pair.encoded.orientation_embeddings[pair.instance_position, pair.rotation_slot],
        _tensor(np.asarray(placement["raster"]), device, dtype=torch.float32),
        _tensor(candidates, device, dtype=torch.float32),
    )
    position, position_log, position_entropy = _masked_choice(
        values, torch.ones_like(values, dtype=torch.bool), generator
    )
    return PolygonPolicyDecision(
        -1,
        pair.instance_position,
        pair.rotation_slot,
        position,
        pair.log_probability + position_log,
        pair.entropy + position_entropy,
        pair.encoded.value,
    )


class PolygonPolicyRunner:
    """Получает проверенные нейросетевые и гибридные полигональные решения."""

    def __init__(self, model: HierarchicalPolygonPolicyV1, *, model_id: str, model_sha256: str,
                 device: str | torch.device = "cpu", revision: str = "unknown") -> None:
        """Сохраняет модель и проверяемую идентичность её весов."""

        if len(model_sha256) != 64 or any(character not in "0123456789abcdef" for character in model_sha256.lower()):
            raise ValueError("`model_sha256` должен содержать 64 шестнадцатеричных символа")
        self.model = model.to(device).eval()
        self.model_id = model_id
        self.model_sha256 = model_sha256.lower()
        self.device = torch.device(device)
        self.revision = revision

    def _provenance(self, family: str, seed: int, rollouts: int, mode: str) -> dict[str, Any]:
        """Формирует сведения о происхождении полигонального решения v2."""

        return {"family": family, "name": "polygon-policy-v1" if family == "neural" else "hybrid-polygon-policy-v1",
                "projectVersion": __version__, "revision": self.revision, "seed": seed,
                "randomIterations": 64, "beamWidth": 8, "maxExpandedStates": 5000, "timeoutMs": 0,
                "modelId": self.model_id, "modelSha256": self.model_sha256, "rollouts": rollouts,
                "selectionMode": mode}

    def _rollout(self, problem: Mapping[str, Any], seed: int, sampled: bool, rollouts: int) -> dict[str, Any]:
        """Выполняет эпизод исключительно через допустимые действия среды C++."""

        environment = PolygonNestingEnv.from_dict(problem, reward_version=2)
        fixed = environment.static_observation()
        dynamic, _ = environment.reset_compact(seed=seed)
        generator = None
        if sampled:
            generator = torch.Generator(device=self.device)
            generator.manual_seed(seed)
        with torch.no_grad():
            while not environment.is_terminal:
                decision = select_polygon_action(self.model, environment, fixed, dynamic, self.device, generator)
                dynamic, _, _, _, _ = environment.step_compact(decision.action_index)
        mode = "sampled-best-of" if sampled else "greedy"
        solution = environment.snapshot_solution(self._provenance("neural", seed, rollouts, mode))
        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(solution))
        if error:
            raise RuntimeError(f"нейросетевое полигональное решение не прошло независимую проверку: {error}")
        return solution

    def solve(
        self,
        problem: Mapping[str, Any],
        *,
        mode: str = "greedy",
        rollouts: int = 16,
        seed: int = 42,
        baseline_solution: Mapping[str, Any] | None = None,
    ) -> dict[str, Any]:
        """Возвращает жадное, лучшее из нескольких либо гибридное решение v2."""

        if mode not in {"greedy", "best-of", "hybrid"}:
            raise ValueError(f"неизвестный режим полигональной политики: {mode}")
        if rollouts < 1:
            raise ValueError("число прогонов политики должно быть положительным")
        if mode == "greedy":
            return self._rollout(problem, seed, False, 1)
        candidates = [self._rollout(problem, seed + index, True, rollouts) for index in range(rollouts)]
        best = candidates[0]
        for candidate in candidates[1:]:
            if _native.is_better_polygon_solution(canonical_json(candidate), canonical_json(best)):
                best = candidate
        if mode == "best-of":
            return best

        if baseline_solution is None:
            baseline = json.loads(_native.solve_polygon_problem(canonical_json(problem), solver="random-left-bottom",
                                                                seed=seed, random_iterations=64, beam_width=8,
                                                                max_expanded_states=5000, timeout_ms=0))
        else:
            baseline = dict(baseline_solution)
            error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(baseline))
            if error:
                raise ValueError(f"резервное полигональное решение не прошло проверку: {error}")
        return self.hybrid_solution(problem, best, baseline, seed=seed, rollouts=rollouts)

    def hybrid_solution(
        self,
        problem: Mapping[str, Any],
        neural_solution: Mapping[str, Any],
        baseline_solution: Mapping[str, Any],
        *,
        seed: int,
        rollouts: int,
    ) -> dict[str, Any]:
        """Публикует лучшее из готовых нейросетевого и базового решений как гибридное."""

        for label, solution in (("нейросетевое", neural_solution), ("базовое", baseline_solution)):
            error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(solution))
            if error:
                raise ValueError(f"{label} полигональное решение не прошло проверку: {error}")
        chosen = (
            baseline_solution
            if _native.is_better_polygon_solution(canonical_json(baseline_solution), canonical_json(neural_solution))
            else neural_solution
        )
        environment = PolygonNestingEnv.from_dict(problem, reward_version=2)
        environment.reset_compact(seed=seed)
        for placement in chosen["placements"]:
            environment.step_compact(environment.find_action(placement))
        result = environment.snapshot_solution(self._provenance("hybrid", seed, rollouts, "hybrid-best-of"))
        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(result))
        if error:
            raise RuntimeError(f"гибридное полигональное решение не прошло независимую проверку: {error}")
        return result
