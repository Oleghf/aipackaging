"""Безопасный иерархический выбор действий и запуск нейросетевого решателя."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Mapping

import numpy as np
import torch
from torch import Tensor

from . import __version__, _aipackaging_solver as _native
from .environment import GridNestingEnv, canonical_json
from .model import EncodedGridState, HierarchicalGridPolicyV1


@dataclass(frozen=True)
class PolicyDecision:
    """Содержит выбранный action index и дифференцируемые значения actor-critic."""

    action_index: int
    instance_index: int
    rotation_slot: int
    position_index: int
    log_probability: Tensor
    entropy: Tensor
    value: Tensor


def _tensor(value: np.ndarray, device: torch.device, *, dtype: torch.dtype) -> Tensor:
    """Копирует read-only NumPy-массив в tensor заданного dtype и устройства."""

    return torch.as_tensor(np.array(value, copy=True), dtype=dtype, device=device)


def encode_observation(
    model: HierarchicalGridPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    device: torch.device,
) -> EncodedGridState:
    """Преобразует observation v1 и запускает общую encoder-часть политики."""

    return model.encode(
        _tensor(dynamic["occupancy"], device, dtype=torch.float32),
        _tensor(fixed["part_masks"], device, dtype=torch.float32),
        _tensor(fixed["part_features"], device, dtype=torch.float32),
        _tensor(dynamic["objective"], device, dtype=torch.float32),
    )


def _masked_choice(logits: Tensor, legal: Tensor, generator: torch.Generator | None) -> tuple[int, Tensor, Tensor]:
    """Выбирает только разрешённый элемент и возвращает log-probability с entropy."""

    indices = torch.nonzero(legal, as_tuple=False).flatten()
    if indices.numel() == 0:
        raise RuntimeError("hierarchical policy level has no legal choices")
    legal_logits = logits.index_select(0, indices)
    probabilities = torch.softmax(legal_logits, dim=0)
    if generator is None:
        # torch.argmax возвращает первый максимум, что закрепляет stable tie-break.
        local = torch.argmax(legal_logits)
    else:
        local = torch.multinomial(probabilities, 1, generator=generator).squeeze(0)
    log_probabilities = torch.log_softmax(legal_logits, dim=0)
    entropy = -(probabilities * log_probabilities).sum()
    return int(indices[local].item()), log_probabilities[local], entropy


def evaluate_action(
    model: HierarchicalGridPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    action_index: int,
    device: torch.device,
) -> PolicyDecision:
    """Вычисляет факторизованную вероятность известного допустимого действия."""

    action_mask = np.asarray(dynamic["action_mask"], dtype=np.bool_)
    if action_index < 0 or action_index >= action_mask.size:
        raise IndexError("policy action index is out of range")
    if not action_mask[action_index]:
        raise ValueError("policy action is masked out")

    candidate_instance = np.asarray(fixed["candidate_instance"], dtype=np.int64)
    candidate_rotation = np.asarray(fixed["candidate_rotation"], dtype=np.int64)
    target_instance = int(candidate_instance[action_index])
    target_rotation = int(candidate_rotation[action_index])
    encoded = encode_observation(model, fixed, dynamic, device)

    legal_instances_np = np.zeros(encoded.instance_logits.shape[0], dtype=np.bool_)
    legal_instances_np[candidate_instance[action_mask]] = True
    legal_instances = _tensor(legal_instances_np, device, dtype=torch.bool)
    legal_instance_indices = torch.nonzero(legal_instances, as_tuple=False).flatten()
    instance_logits = encoded.instance_logits.index_select(0, legal_instance_indices)
    target_instance_local = int(torch.nonzero(legal_instance_indices == target_instance, as_tuple=False).item())
    instance_log_probabilities = torch.log_softmax(instance_logits, dim=0)
    instance_probabilities = torch.softmax(instance_logits, dim=0)

    rotation_mask_np = np.zeros(4, dtype=np.bool_)
    selected_instance_actions = action_mask & (candidate_instance == target_instance)
    rotation_mask_np[candidate_rotation[selected_instance_actions]] = True
    legal_rotations = _tensor(rotation_mask_np, device, dtype=torch.bool)
    legal_rotation_indices = torch.nonzero(legal_rotations, as_tuple=False).flatten()
    rotation_logits = encoded.rotation_logits[target_instance].index_select(0, legal_rotation_indices)
    target_rotation_local = int(torch.nonzero(legal_rotation_indices == target_rotation, as_tuple=False).item())
    rotation_log_probabilities = torch.log_softmax(rotation_logits, dim=0)
    rotation_probabilities = torch.softmax(rotation_logits, dim=0)

    group = np.flatnonzero(selected_instance_actions & (candidate_rotation == target_rotation))
    position_index = int(np.flatnonzero(group == action_index)[0])
    candidate_features = _tensor(np.asarray(fixed["candidate_features"])[group], device, dtype=torch.float32)
    position_logits = model.positions(
        encoded.state_embedding, encoded.orientation_embeddings[target_instance, target_rotation], candidate_features
    )
    position_log_probabilities = torch.log_softmax(position_logits, dim=0)
    position_probabilities = torch.softmax(position_logits, dim=0)
    entropy = (
        -(instance_probabilities * instance_log_probabilities).sum()
        -(rotation_probabilities * rotation_log_probabilities).sum()
        -(position_probabilities * position_log_probabilities).sum()
    )
    log_probability = (
        instance_log_probabilities[target_instance_local]
        + rotation_log_probabilities[target_rotation_local]
        + position_log_probabilities[position_index]
    )
    return PolicyDecision(
        action_index,
        target_instance,
        target_rotation,
        position_index,
        log_probability,
        entropy,
        encoded.value,
    )


def select_action(
    model: HierarchicalGridPolicyV1,
    fixed: Mapping[str, Any],
    dynamic: Mapping[str, Any],
    device: torch.device,
    generator: torch.Generator | None = None,
) -> PolicyDecision:
    """Выбирает допустимое действие по трём маскированным уровням политики."""

    action_mask = np.asarray(dynamic["action_mask"], dtype=np.bool_)
    candidate_instance = np.asarray(fixed["candidate_instance"], dtype=np.int64)
    candidate_rotation = np.asarray(fixed["candidate_rotation"], dtype=np.int64)
    if not np.any(action_mask):
        raise RuntimeError("cannot select an action in a terminal state")
    encoded = encode_observation(model, fixed, dynamic, device)

    instance_mask_np = np.zeros(encoded.instance_logits.shape[0], dtype=np.bool_)
    instance_mask_np[candidate_instance[action_mask]] = True
    instance, instance_log_probability, instance_entropy = _masked_choice(
        encoded.instance_logits, _tensor(instance_mask_np, device, dtype=torch.bool), generator
    )

    instance_actions = action_mask & (candidate_instance == instance)
    rotation_mask_np = np.zeros(4, dtype=np.bool_)
    rotation_mask_np[candidate_rotation[instance_actions]] = True
    rotation, rotation_log_probability, rotation_entropy = _masked_choice(
        encoded.rotation_logits[instance], _tensor(rotation_mask_np, device, dtype=torch.bool), generator
    )

    group = np.flatnonzero(instance_actions & (candidate_rotation == rotation))
    candidate_features = _tensor(np.asarray(fixed["candidate_features"])[group], device, dtype=torch.float32)
    position_logits = model.positions(encoded.state_embedding, encoded.orientation_embeddings[instance, rotation], candidate_features)
    legal_positions = torch.ones(position_logits.shape[0], dtype=torch.bool, device=device)
    position, position_log_probability, position_entropy = _masked_choice(position_logits, legal_positions, generator)
    return PolicyDecision(
        int(group[position]),
        instance,
        rotation,
        position,
        instance_log_probability + rotation_log_probability + position_log_probability,
        instance_entropy + rotation_entropy + position_entropy,
        encoded.value,
    )


class PolicyRunner:
    """Получает решения нейросетью и опционально объединяет их с безопасным baseline."""

    def __init__(
        self,
        model: HierarchicalGridPolicyV1,
        *,
        model_id: str,
        model_sha256: str,
        device: str | torch.device = "cpu",
        revision: str = "unknown",
        limits: Mapping[str, int] | None = None,
    ) -> None:
        """Сохраняет модель и проверяемую идентичность её весов."""

        if len(model_sha256) != 64 or any(character not in "0123456789abcdef" for character in model_sha256.lower()):
            raise ValueError("model_sha256 must contain 64 hexadecimal characters")
        self.model = model.to(device)
        self.model.eval()
        self.model_id = model_id
        self.model_sha256 = model_sha256.lower()
        self.device = torch.device(device)
        self.revision = revision
        self.limits = dict(
            limits
            or {"maxRows": 24, "maxColumns": 24, "maxInstances": 30, "maxPartExtent": 12, "maxActions": 65536}
        )

    def _check_limits(self, fixed: Mapping[str, Any]) -> None:
        """Отклоняет observation вне заявленного диапазона обученной модели."""

        actual = {
            "maxRows": int(fixed["rows"]),
            "maxColumns": int(fixed["columns"]),
            "maxInstances": int(np.asarray(fixed["part_features"]).shape[0]),
            "maxPartExtent": max(int(fixed["max_part_rows"]), int(fixed["max_part_columns"])),
            "maxActions": int(np.asarray(fixed["candidate_instance"]).shape[0]),
        }
        exceeded = [name for name, value in actual.items() if value > self.limits[name]]
        if exceeded:
            raise ValueError(f"problem exceeds grid_policy limits: {', '.join(exceeded)}")

    def _provenance(self, family: str, seed: int, rollouts: int, mode: str) -> dict[str, Any]:
        """Формирует полный нативный provenance для solution v2."""

        return {
            "family": family,
            "name": "grid-policy-v1" if family == "neural" else "hybrid-grid-policy-v1",
            "projectVersion": __version__,
            "revision": self.revision,
            "seed": seed,
            "randomIterations": 64,
            "beamWidth": 32,
            "maxExpandedStates": 50000,
            "timeoutMs": 0,
            "modelId": self.model_id,
            "modelSha256": self.model_sha256,
            "rollouts": rollouts,
            "selectionMode": mode,
        }

    def _rollout(self, problem: Mapping[str, Any], seed: int, sampled: bool, rollouts: int) -> dict[str, Any]:
        """Выполняет один эпизод только через допустимые действия нативной среды."""

        environment = GridNestingEnv.from_dict(problem)
        fixed = environment.static_observation()
        self._check_limits(fixed)
        dynamic, _ = environment.reset_compact(seed=seed)
        generator = None
        if sampled:
            generator = torch.Generator(device=self.device)
            generator.manual_seed(seed)
        with torch.no_grad():
            while not environment.is_terminal:
                decision = select_action(self.model, fixed, dynamic, self.device, generator)
                dynamic, _, _, _, _ = environment.step_compact(decision.action_index)
        provenance = self._provenance("neural", seed, rollouts, "sampled-best-of" if sampled else "greedy")
        incomplete_status = "no_solution_found" if environment.is_dead_end else "budget_exhausted"
        solution = environment.snapshot_solution(provenance, incomplete_status=incomplete_status)
        error = _native.validate_solution(canonical_json(problem), canonical_json(solution))
        if error:
            raise RuntimeError(f"neural solution failed independent validation: {error}")
        return solution

    def _relabel_hybrid(self, problem: Mapping[str, Any], solution: Mapping[str, Any], seed: int, rollouts: int) -> dict[str, Any]:
        """Повторно проигрывает выбранный результат и присваивает честный hybrid provenance."""

        environment = GridNestingEnv.from_dict(problem)
        environment.reset_compact(seed=seed)
        for placement in solution["placements"]:
            environment.step_compact(environment.find_action(placement))
        incomplete_status = "no_solution_found" if environment.is_dead_end else "budget_exhausted"
        result = environment.snapshot_solution(
            self._provenance("hybrid", seed, rollouts, "hybrid-best-of"),
            incomplete_status=incomplete_status,
        )
        error = _native.validate_solution(canonical_json(problem), canonical_json(result))
        if error:
            raise RuntimeError(f"hybrid solution failed independent validation: {error}")
        return result

    def solve(self, problem: Mapping[str, Any], *, mode: str = "greedy", rollouts: int = 16, seed: int = 42) -> dict[str, Any]:
        """Возвращает neural greedy, neural best-of или hybrid grid_solution v2."""

        if mode not in {"greedy", "best-of", "hybrid"}:
            raise ValueError(f"unknown policy solve mode: {mode}")
        if rollouts < 1:
            raise ValueError("rollouts must be positive")
        if mode == "greedy":
            return self._rollout(problem, seed, False, 1)

        candidates = [self._rollout(problem, seed + index, True, rollouts) for index in range(rollouts)]
        best = candidates[0]
        for candidate in candidates[1:]:
            if _native.is_better_solution(canonical_json(candidate), canonical_json(best)):
                best = candidate
        if mode == "best-of":
            return best

        baseline = json.loads(
            _native.solve_problem(
                canonical_json(problem),
                solver="random-left-bottom",
                seed=seed,
                random_iterations=64,
                beam_width=32,
                max_expanded_states=50000,
                timeout_ms=0,
            )
        )
        if _native.is_better_solution(canonical_json(baseline), canonical_json(best)):
            best = baseline
        return self._relabel_hybrid(problem, best, seed, rollouts)
