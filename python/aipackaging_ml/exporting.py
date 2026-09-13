"""Экспорт двухчастной политики v1 в ONNX и проверка метаданных комплекта."""

from __future__ import annotations

import warnings
from pathlib import Path
from typing import Any

import torch
from torch import Tensor, nn

from . import __version__
from .contracts import load_training_config, sha256_file, write_canonical_json
from .model import GridStateEncoder, HierarchicalGridPolicyV1
from .model_bundle import bundle_digest
from .training import load_checkpoint


class EncoderExport(nn.Module):
    """Адаптирует кодировщик NamedTuple к стабильным именованным выходам ONNX."""

    def __init__(self, encoder: GridStateEncoder) -> None:
        """Сохраняет обученный кодировщик без копирования его параметров."""

        super().__init__()
        self.encoder = encoder

    def forward(
        self, occupancy: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor
    ) -> tuple[Tensor, Tensor, Tensor, Tensor, Tensor]:
        """Возвращает пять тензоров в порядке публичного контракта ONNX."""

        encoded = self.encoder(occupancy, part_masks, part_features, objective)
        return (
            encoded.state_embedding,
            encoded.orientation_embeddings,
            encoded.instance_logits,
            encoded.rotation_logits,
            encoded.value,
        )


def _export_onnx(*args: Any, **kwargs: Any) -> None:
    """Экспортирует граф, скрывая известное ложное предупреждение общей оси."""

    with warnings.catch_warnings():
    # Один объект размерности намеренно связывает ось экземпляров трёх тензоров. Экспортёр
        # сообщает, что повторное имя не переименовано, хотя связь сохранена.
        warnings.filterwarnings("ignore", message=r"# The axis name: instances will not be used.*")
        torch.onnx.export(*args, **kwargs)


def export_policy_bundle(
    checkpoint: str | Path,
    config_path: str | Path,
    output: str | Path,
    *,
    model_id: str = "grid-policy-v1",
) -> dict[str, Any]:
    """Экспортирует кодировщик и голову, считает хеши и записывает метаданные `grid_policy` v1."""

    config = load_training_config(config_path)
    destination = Path(output)
    destination.mkdir(parents=True, exist_ok=True)
    model = HierarchicalGridPolicyV1(config["model"]["hiddenSize"])
    load_checkpoint(checkpoint, model, torch.device("cpu"))
    model.eval()
    hidden = config["model"]["hiddenSize"]
    encoder_path = destination / "encoder.onnx"
    head_path = destination / "placement-head.onnx"
    for stale_sidecar in (destination / "encoder.onnx.data", destination / "placement-head.onnx.data"):
        stale_sidecar.unlink(missing_ok=True)

    dummy_encoder = (
        torch.zeros((8, 8), dtype=torch.float32),
        torch.zeros((2, 4, 2, 2), dtype=torch.float32),
        torch.zeros((2, 7), dtype=torch.float32),
        torch.zeros((7,), dtype=torch.float32),
    )
    rows = torch.export.Dim("rows", min=1, max=config["model"]["maxRows"])
    columns = torch.export.Dim("columns", min=1, max=config["model"]["maxColumns"])
    instances = torch.export.Dim("instances", min=1, max=config["model"]["maxInstances"])
    part_rows = torch.export.Dim("part_rows", min=1, max=config["model"]["maxPartExtent"])
    part_columns = torch.export.Dim("part_columns", min=1, max=config["model"]["maxPartExtent"])
    _export_onnx(
        EncoderExport(model.encoder).eval(),
        dummy_encoder,
        encoder_path,
        input_names=["occupancy", "part_masks", "part_features", "objective"],
        output_names=["state_embedding", "orientation_embeddings", "instance_logits", "rotation_logits", "value"],
        dynamic_shapes=(
            {0: rows, 1: columns},
            {0: instances, 2: part_rows, 3: part_columns},
            {0: instances},
            None,
        ),
        opset_version=config["export"]["opset"],
        dynamo=True,
        verbose=False,
        external_data=False,
    )
    candidates = torch.export.Dim("candidates", min=1, max=config["model"]["maxActions"])
    _export_onnx(
        model.placement_head,
        (torch.zeros(hidden), torch.zeros(hidden), torch.zeros((4, 7))),
        head_path,
        input_names=["state_embedding", "orientation_embedding", "candidate_features"],
        output_names=["position_logits"],
        dynamic_shapes=(None, None, {0: candidates}),
        opset_version=config["export"]["opset"],
        dynamo=True,
        verbose=False,
        external_data=False,
    )

    files = {encoder_path.name: sha256_file(encoder_path), head_path.name: sha256_file(head_path)}
    metadata = {
        "format": "aipackaging.grid_policy",
        "version": 1,
        "modelId": model_id,
        "projectVersion": __version__,
        "architecture": config["model"]["architecture"],
        "opset": config["export"]["opset"],
        "contracts": {"problem": 1, "solution": 2, "observation": 1, "action": 1, "reward": 1},
        "limits": {
            "maxRows": config["model"]["maxRows"],
            "maxColumns": config["model"]["maxColumns"],
            "maxInstances": config["model"]["maxInstances"],
            "maxPartExtent": config["model"]["maxPartExtent"],
            "maxActions": config["model"]["maxActions"],
        },
        "normalization": "observation-v1",
        "selection": {"hierarchy": ["instance", "rotation", "position"], "tieBreak": "lowest-stable-action-index"},
        "dynamicAxes": ["rows", "columns", "instances", "part_rows", "part_columns", "candidates"],
        "datasetManifestSha256": config["datasetManifestSha256"],
        "trainingConfigSha256": sha256_file(config_path),
        "checkpointSha256": sha256_file(checkpoint),
        "files": files,
        "modelSha256": bundle_digest(files),
    }
    write_canonical_json(destination / "metadata.json", metadata)
    return metadata
