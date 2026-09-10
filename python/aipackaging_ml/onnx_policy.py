"""CPU-инференс экспортированной ONNX policy с точным action mask M2."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Mapping

import numpy as np

from .model_bundle import load_model_metadata


class OnnxGreedyPolicy:
    """Выбирает greedy-действия двумя ONNX-графами без зависимости от PyTorch."""

    def __init__(self, bundle: str | Path) -> None:
        """Проверяет bundle и создаёт CPU ONNX Runtime sessions."""

        import onnxruntime as ort

        self.root = Path(bundle)
        self.metadata = load_model_metadata(self.root)
        providers = ["CPUExecutionProvider"]
        self.encoder = ort.InferenceSession(str(self.root / "encoder.onnx"), providers=providers)
        self.head = ort.InferenceSession(str(self.root / "placement-head.onnx"), providers=providers)

    @staticmethod
    def _argmax(values: np.ndarray, legal: np.ndarray) -> int:
        """Возвращает первый максимальный разрешённый индекс."""

        indices = np.flatnonzero(legal)
        if indices.size == 0:
            raise RuntimeError("ONNX policy level has no legal choices")
        return int(indices[int(np.argmax(values[indices]))])

    def action(self, fixed: Mapping[str, Any], dynamic: Mapping[str, Any]) -> int:
        """Возвращает стабильный допустимый action index для observation v1."""

        outputs = self.encode(fixed, dynamic)
        state, orientations, instance_logits, rotation_logits, _ = outputs
        action_mask = np.asarray(dynamic["action_mask"], dtype=np.bool_)
        candidate_instance = np.asarray(fixed["candidate_instance"], dtype=np.int64)
        candidate_rotation = np.asarray(fixed["candidate_rotation"], dtype=np.int64)
        instance_mask = np.zeros(instance_logits.shape[0], dtype=np.bool_)
        instance_mask[candidate_instance[action_mask]] = True
        instance = self._argmax(instance_logits, instance_mask)
        instance_actions = action_mask & (candidate_instance == instance)
        rotation_mask = np.zeros(4, dtype=np.bool_)
        rotation_mask[candidate_rotation[instance_actions]] = True
        rotation = self._argmax(rotation_logits[instance], rotation_mask)
        group = np.flatnonzero(instance_actions & (candidate_rotation == rotation))
        position_logits = self.position_scores(state, orientations[instance, rotation], np.asarray(fixed["candidate_features"])[group])
        return int(group[int(np.argmax(position_logits))])

    def encode(self, fixed: Mapping[str, Any], dynamic: Mapping[str, Any]) -> list[np.ndarray]:
        """Возвращает сырые выходы encoder для parity-проверки и выбора действия."""

        return self.encoder.run(
            None,
            {
                "occupancy": np.asarray(dynamic["occupancy"], dtype=np.float32),
                "part_masks": np.asarray(fixed["part_masks"], dtype=np.float32),
                "part_features": np.asarray(fixed["part_features"], dtype=np.float32),
                "objective": np.asarray(dynamic["objective"], dtype=np.float32),
            },
        )

    def position_scores(
        self, state: np.ndarray, orientation: np.ndarray, candidate_features: np.ndarray
    ) -> np.ndarray:
        """Возвращает сырые logits позиционной головы для выбранной группы."""

        return self.head.run(
            None,
            {
                "state_embedding": np.asarray(state, dtype=np.float32),
                "orientation_embedding": np.asarray(orientation, dtype=np.float32),
                "candidate_features": np.asarray(candidate_features, dtype=np.float32),
            },
        )[0]
