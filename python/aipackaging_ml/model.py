"""Иерархическая модель «исполнитель-критик» для клеточного раскроя M3."""

from __future__ import annotations

from typing import NamedTuple

import torch
from torch import Tensor, nn


class EncodedGridState(NamedTuple):
    """Содержит переиспользуемые представления состояния и выходы первых голов."""

    state_embedding: Tensor
    orientation_embeddings: Tensor
    instance_logits: Tensor
    rotation_logits: Tensor
    value: Tensor


class GridStateEncoder(nn.Module):
    """Кодирует лист, геометрию экземпляров и целевую функцию первых двух решений."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт кодировщик фиксированной ширины с общими весами для всех деталей."""

        super().__init__()
        if hidden_size < 2 or hidden_size % 2 != 0:
            raise ValueError("`hidden_size` должен быть чётным целым числом не меньше 2")
        half = hidden_size // 2
        self.sheet_encoder = nn.Sequential(
            nn.Conv2d(1, 32, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(32, 64, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(64, hidden_size, 3, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
        )
        self.shape_encoder = nn.Sequential(
            nn.Conv2d(1, 16, 3, padding=1),
            nn.ReLU(),
            nn.Conv2d(16, half, 3, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
        )
        self.part_feature_encoder = nn.Sequential(nn.Linear(7, half), nn.ReLU(), nn.Linear(half, half), nn.ReLU())
        self.orientation_fusion = nn.Sequential(nn.Linear(hidden_size, hidden_size), nn.ReLU())
        self.objective_encoder = nn.Sequential(nn.Linear(7, half), nn.ReLU(), nn.Linear(half, hidden_size), nn.ReLU())
        self.state_fusion = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.ReLU())
        self.instance_head = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))
        self.rotation_head = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))
        self.value_head = nn.Sequential(nn.Linear(hidden_size, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))

    def forward(self, occupancy: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor) -> EncodedGridState:
        """Возвращает вложения, логиты экземпляров и поворотов, а также оценку состояния."""

        # Внешний контракт не содержит пакетного измерения: один эпизод имеет
        # переменное число экземпляров и собственные размеры листа/деталей.
        sheet = self.sheet_encoder(occupancy.unsqueeze(0).unsqueeze(0)).flatten()
        count, rotations, rows, columns = part_masks.shape
        shapes = self.shape_encoder(part_masks.reshape(count * rotations, 1, rows, columns)).reshape(count, rotations, -1)
        features = self.part_feature_encoder(part_features).unsqueeze(1).expand(-1, rotations, -1)
        orientations = self.orientation_fusion(torch.cat((shapes, features), dim=-1))
        state = self.state_fusion(torch.cat((sheet, self.objective_encoder(objective)), dim=-1))
        expanded = state.unsqueeze(0).expand(count, -1)
        instances = orientations.mean(dim=1)
        instance_logits = self.instance_head(torch.cat((instances, expanded), dim=-1)).squeeze(-1)
        rotation_state = state.reshape(1, 1, -1).expand(count, rotations, -1)
        rotation_logits = self.rotation_head(torch.cat((orientations, rotation_state), dim=-1)).squeeze(-1)
        return EncodedGridState(state, orientations, instance_logits, rotation_logits, self.value_head(state).squeeze(-1))


class GridPlacementHead(nn.Module):
    """Ранжирует только допустимые позиции уже выбранных экземпляра и поворота."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт MLP, общий для всех размеров листа и числа позиций."""

        super().__init__()
        self.candidate_encoder = nn.Sequential(nn.Linear(7, hidden_size), nn.ReLU(), nn.Linear(hidden_size, hidden_size), nn.ReLU())
        self.scorer = nn.Sequential(nn.Linear(hidden_size * 3, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))

    def forward(self, state_embedding: Tensor, orientation_embedding: Tensor, candidate_features: Tensor) -> Tensor:
        """Возвращает один логит для каждой переданной позиции в стабильном порядке."""

        count = candidate_features.shape[0]
        candidates = self.candidate_encoder(candidate_features)
        state = state_embedding.unsqueeze(0).expand(count, -1)
        orientation = orientation_embedding.unsqueeze(0).expand(count, -1)
        return self.scorer(torch.cat((state, orientation, candidates), dim=-1)).squeeze(-1)


class HierarchicalGridPolicyV1(nn.Module):
    """Объединяет кодировщик, иерархические головы политики и критик политики v1."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт политику с заданной шириной скрытого представления."""

        super().__init__()
        self.hidden_size = hidden_size
        self.encoder = GridStateEncoder(hidden_size)
        self.placement_head = GridPlacementHead(hidden_size)

    def encode(self, occupancy: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor) -> EncodedGridState:
        """Делегирует кодирование состояния кодирующей части экспортируемой модели."""

        return self.encoder(occupancy, part_masks, part_features, objective)

    def positions(self, state_embedding: Tensor, orientation_embedding: Tensor, candidate_features: Tensor) -> Tensor:
        """Делегирует ранжирование выбранной группы позиционной голове."""

        return self.placement_head(state_embedding, orientation_embedding, candidate_features)
