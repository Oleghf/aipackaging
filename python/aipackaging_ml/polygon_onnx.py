"""Получение вывода полигональной политики через ONNX Runtime на CPU."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

import numpy as np

from . import __version__, _aipackaging_solver as _native
from .datasets.serialization import canonical_json
from .environment import PolygonNestingEnv
from .polygon_exporting import load_polygon_model_metadata
from .polygon_sampling import SplitMix64, rollout_seed, select_logit


class OnnxPolygonPolicy:
    """Выполняет два графа ONNX только над допустимыми действиями среды C++."""

    def __init__(self, root: str | Path) -> None:
        """Проверяет комплект и создаёт последовательные сеансы CPU."""

        import onnxruntime as ort

        self.root = Path(root)
        self.metadata = load_polygon_model_metadata(self.root)
        options = ort.SessionOptions()
        options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        options.intra_op_num_threads = 1
        options.inter_op_num_threads = 1
        options.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_EXTENDED
        providers = ["CPUExecutionProvider"]
        self.encoder = ort.InferenceSession(str(self.root / "encoder.onnx"), sess_options=options, providers=providers)
        self.placement = ort.InferenceSession(
            str(self.root / "placement-head.onnx"), sess_options=options, providers=providers
        )

    def encode(self, fixed: Mapping[str, Any], dynamic: Mapping[str, Any]) -> tuple[np.ndarray, ...]:
        """Вычисляет общие представления и оценки экземпляров и поворотов."""

        sheet = np.stack((dynamic["occupied"], dynamic["clearance"]), axis=0).astype(np.float32, copy=False)
        return tuple(
            self.encoder.run(
                None,
                {
                    "sheet": sheet,
                    "part_masks": np.asarray(fixed["part_masks"], dtype=np.float32),
                    "part_features": np.asarray(fixed["part_features"], dtype=np.float32),
                    "objective": np.asarray(dynamic["objective"], dtype=np.float32),
                },
            )
        )

    def position_logits(
        self,
        state: np.ndarray,
        orientation: np.ndarray,
        placement: Mapping[str, Any],
    ) -> np.ndarray:
        """Оценивает позиции одной допустимой пары экземпляр/поворот."""

        return self.placement.run(
            None,
            {
                "state_embedding": np.asarray(state, dtype=np.float32),
                "orientation_embedding": np.asarray(orientation, dtype=np.float32),
                "raster": np.asarray(placement["raster"], dtype=np.float32),
                "candidate_features": np.asarray(placement["candidate_features"], dtype=np.float32),
            },
        )[0]

    def action(
        self,
        environment: PolygonNestingEnv,
        fixed: Mapping[str, Any],
        dynamic: Mapping[str, Any],
        generator: SplitMix64 | None = None,
    ) -> int:
        """Выбирает индекс текущего каталога по трём иерархическим уровням."""

        state, orientations, instance_logits, rotation_logits, _ = self.encode(fixed, dynamic)
        pair_mask = np.asarray(dynamic["pair_mask"], dtype=np.bool_)
        instance = select_logit(instance_logits.tolist(), pair_mask.any(axis=1).tolist(), generator)
        rotation = select_logit(rotation_logits[instance].tolist(), pair_mask[instance].tolist(), generator)
        placement = environment.placement_observation(instance, rotation * 90)
        logits = self.position_logits(state, orientations[instance, rotation], placement)
        position = select_logit(logits.tolist(), [True] * len(logits), generator)
        return environment.find_action(placement["actions"][position])


class OnnxPolygonPolicyRunner:
    """Получает проверенные нейросетевые и гибридные решения с ONNX Runtime."""

    def __init__(self, model: OnnxPolygonPolicy, *, revision: str = "unknown") -> None:
        """Сохраняет проверенный комплект и версию нативной среды."""

        self.model = model
        self.revision = revision

    def _provenance(self, family: str, seed: int, rollouts: int, mode: str) -> dict[str, Any]:
        """Формирует сведения о происхождении решения v2."""

        return {
            "family": family,
            "name": "polygon-onnx-policy-v1" if family == "neural" else "hybrid-polygon-onnx-policy-v1",
            "projectVersion": __version__,
            "revision": self.revision,
            "seed": seed,
            "randomIterations": 64,
            "beamWidth": 8,
            "maxExpandedStates": 5000,
            "timeoutMs": 0,
            "modelId": self.model.metadata["modelId"],
            "modelSha256": self.model.metadata["modelSha256"],
            "rollouts": rollouts,
            "selectionMode": mode,
        }

    def _rollout(self, problem: Mapping[str, Any], seed: int, sampled: bool, rollouts: int) -> dict[str, Any]:
        """Проигрывает эпизод, применяя только текущие действия среды C++."""

        environment = PolygonNestingEnv.from_dict(problem, reward_version=2, catalog_version=1)
        fixed = environment.static_observation()
        dynamic, _ = environment.reset_compact(seed=seed)
        generator = SplitMix64(seed) if sampled else None
        while not environment.is_terminal:
            action = self.model.action(environment, fixed, dynamic, generator)
            dynamic, _, _, _, _ = environment.step_compact(action)
        mode = "sampled-best-of" if sampled else "greedy"
        solution = environment.snapshot_solution(self._provenance("neural", seed, rollouts, mode))
        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(solution))
        if error:
            raise RuntimeError(f"решение ONNX не прошло независимую проверку: {error}")
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
        """Возвращает жадное, лучшее из нескольких либо гибридное решение."""

        if mode not in {"greedy", "best-of", "hybrid"}:
            raise ValueError(f"неизвестный режим полигональной модели ONNX: {mode}")
        if rollouts < 1:
            raise ValueError("число нейросетевых прогонов должно быть положительным")
        if mode == "greedy":
            return self._rollout(problem, seed, False, 1)
        candidates = [
            self._rollout(problem, rollout_seed(seed, index), True, rollouts) for index in range(rollouts)
        ]
        best = candidates[0]
        for candidate in candidates[1:]:
            if _native.is_better_polygon_solution(canonical_json(candidate), canonical_json(best)):
                best = candidate
        if mode == "best-of":
            return best
        if baseline_solution is None:
            import json

            baseline_solution = json.loads(
                _native.solve_polygon_problem(
                    canonical_json(problem), solver="random-left-bottom", seed=seed, random_iterations=64,
                    beam_width=8, max_expanded_states=5000, timeout_ms=0, catalog_version=1,
                )
            )
        return self.hybrid_solution(problem, best, baseline_solution, seed=seed, rollouts=rollouts)

    def hybrid_solution(
        self,
        problem: Mapping[str, Any],
        neural_solution: Mapping[str, Any],
        baseline_solution: Mapping[str, Any],
        *,
        seed: int,
        rollouts: int,
    ) -> dict[str, Any]:
        """Сравнивает готовые решения и повторно строит проверенный гибридный итог."""

        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(baseline_solution))
        if error:
            raise ValueError(f"резервное решение не прошло проверку: {error}")
        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(neural_solution))
        if error:
            raise ValueError(f"нейросетевое решение не прошло проверку: {error}")
        chosen = (
            baseline_solution
            if _native.is_better_polygon_solution(canonical_json(baseline_solution), canonical_json(neural_solution))
            else neural_solution
        )
        environment = PolygonNestingEnv.from_dict(problem, reward_version=2, catalog_version=1)
        environment.reset_compact(seed=seed)
        for action in chosen["placements"]:
            environment.step_compact(environment.find_action(action))
        solution = environment.snapshot_solution(self._provenance("hybrid", seed, rollouts, "hybrid-best-of"))
        error = _native.validate_polygon_solution(canonical_json(problem), canonical_json(solution))
        if error:
            raise RuntimeError(f"гибридное решение ONNX не прошло независимую проверку: {error}")
        return solution
