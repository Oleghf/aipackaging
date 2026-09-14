"""Иерархическая нейросетевая политика для полигонального раскроя."""

from __future__ import annotations

from typing import NamedTuple

import torch
from torch import Tensor, nn


class EncodedPolygonState(NamedTuple):
    """Хранит состояние, представления ориентаций, оценки голов и критика."""

    state_embedding: Tensor
    orientation_embeddings: Tensor
    instance_logits: Tensor
    rotation_logits: Tensor
    value: Tensor


class _RasterEncoder(nn.Module):
    """Сжимает растровые каналы в вектор фиксированной ширины."""

    def __init__(self, channels: int, hidden_size: int) -> None:
        """Создаёт свёрточный кодировщик с уменьшением пространственного разрешения."""

        super().__init__()
        self.layers = nn.Sequential(
            nn.Conv2d(channels, 32, 5, stride=2, padding=2),
            nn.ReLU(),
            nn.Conv2d(32, 64, 3, stride=2, padding=1),
            nn.ReLU(),
            nn.Conv2d(64, hidden_size, 3, stride=2, padding=1),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d(1),
        )

    def forward(self, value: Tensor) -> Tensor:
        """Возвращает по одному вектору для каждого входного растра."""

        return self.layers(value).flatten(1)


class PolygonStateEncoder(nn.Module):
    """Кодирует лист, ориентации деталей и текущую целевую функцию."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт кодировщик общей ширины для переменного числа экземпляров."""

        super().__init__()
        if hidden_size < 16 or hidden_size % 2:
            raise ValueError("`hidden_size` должен быть чётным целым числом не меньше 16")
        half = hidden_size // 2
        self.sheet_encoder = _RasterEncoder(2, hidden_size)
        self.shape_encoder = _RasterEncoder(1, half)
        self.part_encoder = nn.Sequential(nn.Linear(7, half), nn.ReLU(), nn.Linear(half, half), nn.ReLU())
        self.orientation_fusion = nn.Sequential(nn.Linear(hidden_size, hidden_size), nn.ReLU())
        self.objective_encoder = nn.Sequential(nn.Linear(7, hidden_size), nn.ReLU(), nn.Linear(hidden_size, hidden_size), nn.ReLU())
        self.state_fusion = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.ReLU())
        self.instance_head = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))
        self.rotation_head = nn.Sequential(nn.Linear(hidden_size * 2, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))
        self.value_head = nn.Sequential(nn.Linear(hidden_size, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))

    def forward(self, sheet: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor) -> EncodedPolygonState:
        """Возвращает оценки экземпляров, поворотов и значения состояния."""

        sheet_embedding = self.sheet_encoder(sheet.unsqueeze(0)).squeeze(0)
        instances, rotations, rows, columns = part_masks.shape
        shapes = self.shape_encoder(part_masks.reshape(instances * rotations, 1, rows, columns)).reshape(instances, rotations, -1)
        features = self.part_encoder(part_features).unsqueeze(1).expand(-1, rotations, -1)
        orientations = self.orientation_fusion(torch.cat((shapes, features), dim=-1))
        state = self.state_fusion(torch.cat((sheet_embedding, self.objective_encoder(objective)), dim=-1))
        expanded = state.unsqueeze(0).expand(instances, -1)
        instance_logits = self.instance_head(torch.cat((orientations.mean(dim=1), expanded), dim=-1)).squeeze(-1)
        rotation_state = state.reshape(1, 1, -1).expand(instances, rotations, -1)
        rotation_logits = self.rotation_head(torch.cat((orientations, rotation_state), dim=-1)).squeeze(-1)
        return EncodedPolygonState(state, orientations, instance_logits, rotation_logits, self.value_head(state).squeeze(-1))


class PolygonPlacementHead(nn.Module):
    """Оценивает кандидатов выбранной пары по условному растру и признакам."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт общий кодировщик запроса и оценочную голову кандидатов."""

        super().__init__()
        self.query_encoder = _RasterEncoder(4, hidden_size)
        self.candidate_encoder = nn.Sequential(nn.Linear(7, hidden_size), nn.ReLU(), nn.Linear(hidden_size, hidden_size), nn.ReLU())
        self.scorer = nn.Sequential(nn.Linear(hidden_size * 4, hidden_size), nn.Tanh(), nn.Linear(hidden_size, 1))

    def forward(self, state: Tensor, orientation: Tensor, raster: Tensor, candidates: Tensor) -> Tensor:
        """Возвращает одну оценку для каждого кандидата в стабильном порядке среды."""

        query = self.query_encoder(raster.unsqueeze(0)).squeeze(0)
        count = candidates.shape[0]
        return self.scorer(
            torch.cat(
                (
                    state.unsqueeze(0).expand(count, -1),
                    orientation.unsqueeze(0).expand(count, -1),
                    query.unsqueeze(0).expand(count, -1),
                    self.candidate_encoder(candidates),
                ),
                dim=-1,
            )
        ).squeeze(-1)


class HierarchicalPolygonPolicyV1(nn.Module):
    """Объединяет три уровня выбора полигонального действия и критик."""

    def __init__(self, hidden_size: int = 128) -> None:
        """Создаёт политику с заданной шириной скрытого представления."""

        super().__init__()
        self.hidden_size = hidden_size
        self.encoder = PolygonStateEncoder(hidden_size)
        self.placement_head = PolygonPlacementHead(hidden_size)

    def encode(self, sheet: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor) -> EncodedPolygonState:
        """Кодирует состояние для первых двух уровней и критика."""

        return self.encoder(sheet, part_masks, part_features, objective)

    def positions(self, state: Tensor, orientation: Tensor, raster: Tensor, candidates: Tensor) -> Tensor:
        """Оценивает позиции выбранных экземпляра и поворота."""

        return self.placement_head(state, orientation, raster, candidates)
