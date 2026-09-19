"""Экспорт полигональной политики в строгий двухчастный комплект ONNX."""

from __future__ import annotations

import json
import warnings
from pathlib import Path
from typing import Any, Mapping

import torch
from torch import Tensor, nn

from . import __version__
from .datasets.serialization import sha256_file, write_canonical_json
from .model_bundle import bundle_digest
from .polygon_contracts import load_polygon_training_config
from .polygon_model import HierarchicalPolygonPolicyV1, PolygonStateEncoder
from .polygon_training import load_polygon_checkpoint


class PolygonEncoderExport(nn.Module):
    """Преобразует именованный результат кодировщика в выходы ONNX."""

    def __init__(self, encoder: PolygonStateEncoder) -> None:
        """Сохраняет обученный кодировщик без копирования параметров."""

        super().__init__()
        self.encoder = encoder

    def forward(
        self, sheet: Tensor, part_masks: Tensor, part_features: Tensor, objective: Tensor
    ) -> tuple[Tensor, Tensor, Tensor, Tensor, Tensor]:
        """Возвращает представления, оценки двух уровней и значение состояния."""

        encoded = self.encoder(sheet, part_masks, part_features, objective)
        return (
            encoded.state_embedding,
            encoded.orientation_embeddings,
            encoded.instance_logits,
            encoded.rotation_logits,
            encoded.value,
        )


def _export_onnx(*args: Any, **kwargs: Any) -> None:
    """Экспортирует граф и скрывает известное предупреждение общей динамической оси."""

    with warnings.catch_warnings():
        warnings.filterwarnings("ignore", message=r"# The axis name: instances will not be used.*")
        torch.onnx.export(*args, **kwargs)


def _stamp_graph(path: Path, graph_kind: str) -> None:
    """Записывает проверяемые свойства контракта непосредственно в граф ONNX."""

    import onnx

    model = onnx.load(path, load_external_data=False)
    onnx.helper.set_model_props(
        model,
        {
            "aipackaging.architecture": "hierarchical-polygon-policy-v1",
            "aipackaging.graph": graph_kind,
            "aipackaging.opset": "23",
        },
    )
    onnx.save(model, path, save_as_external_data=False)


def _metadata_fields() -> set[str]:
    """Возвращает строгий набор верхнеуровневых полей комплекта v2."""

    return {
        "format", "version", "modelId", "projectVersion", "architecture", "opset", "contracts", "limits",
        "normalization", "selection", "dynamicAxes", "datasetManifestSha256", "trainingConfigSha256",
        "checkpointSha256", "files", "modelSha256",
    }


def load_polygon_model_metadata(path: str | Path) -> dict[str, Any]:
    """Строго загружает метаданные v2 и проверяет SHA-256 обоих графов."""

    root = Path(path)
    metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
    if not isinstance(metadata, dict) or set(metadata) != _metadata_fields() or metadata.get("format") != "aipackaging.polygon_policy" or metadata.get("version") != 2:
        raise ValueError("неподдерживаемые или нестрогие метаданные полигональной модели")
    if not isinstance(metadata["modelId"], str) or not metadata["modelId"] or not isinstance(metadata["projectVersion"], str) or not metadata["projectVersion"]:
        raise ValueError("идентичность полигональной модели некорректна")
    if metadata["architecture"] != "hierarchical-polygon-policy-v1" or metadata["opset"] != 23:
        raise ValueError("архитектура или набор операций полигональной модели несовместимы")
    if metadata["contracts"] != {"problem": 1, "solution": 2, "observation": 2, "action": 1, "reward": 2}:
        raise ValueError("версии контрактов полигональной модели несовместимы")
    limits = metadata["limits"]
    if not isinstance(limits, dict) or set(limits) != {"hiddenSize", "sheetRasterSize", "shapeRasterSize", "maxInstances", "maxCandidatesPerPair"}:
        raise ValueError("ограничения полигональной модели содержат неверные поля")
    if limits["sheetRasterSize"] != 128 or limits["shapeRasterSize"] != 32:
        raise ValueError("размеры растров полигональной модели несовместимы")
    if (
        any(type(limits[name]) is not int for name in ("hiddenSize", "maxInstances", "maxCandidatesPerPair"))
        or limits["hiddenSize"] < 16 or limits["hiddenSize"] % 2
        or not 1 <= limits["maxInstances"] <= 100
        or not 1 <= limits["maxCandidatesPerPair"] <= 250_000
    ):
        raise ValueError("числовые ограничения полигональной модели несовместимы")
    selection = metadata["selection"]
    expected_selection = {
        "hierarchy": ["instance", "rotation", "position"],
        "tieBreak": "lowest-current-catalog-index",
        "sampling": "splitmix64-categorical-v1",
    }
    if selection != expected_selection:
        raise ValueError("правила выбора полигональной модели несовместимы")
    if metadata["dynamicAxes"] != ["instances", "candidates"] or metadata["normalization"] != "polygon-observation-v2":
        raise ValueError("контракт наблюдения полигональной модели несовместим")
    if not isinstance(metadata["files"], dict) or set(metadata["files"]) != {"encoder.onnx", "placement-head.onnx"}:
        raise ValueError("комплект полигональной модели содержит неверный набор графов")
    expected_files = {"metadata.json", "encoder.onnx", "placement-head.onnx"}
    if {item.name for item in root.iterdir()} != expected_files or any(not (root / name).is_file() for name in expected_files):
        raise ValueError("каталог модели должен содержать ровно три файла комплекта")
    hashes = [
        metadata["datasetManifestSha256"], metadata["trainingConfigSha256"], metadata["checkpointSha256"],
        metadata["modelSha256"], *metadata["files"].values(),
    ]
    if any(not isinstance(value, str) or len(value) != 64 or any(c not in "0123456789abcdef" for c in value) for value in hashes):
        raise ValueError("метаданные полигональной модели содержат некорректный SHA-256")
    actual = {name: sha256_file(root / name) for name in sorted(metadata["files"])}
    if actual != metadata["files"] or bundle_digest(actual) != metadata["modelSha256"]:
        raise ValueError("контрольная сумма комплекта полигональной модели не совпадает")

    import onnx

    for name in sorted(actual):
        model = onnx.load(root / name, load_external_data=False)
        default_opset = next((item.version for item in model.opset_import if not item.domain), None)
        if default_opset != metadata["opset"]:
            raise ValueError(f"версия набора операций ONNX не совпадает в {name}")
        expected_properties = {
            "aipackaging.architecture": "hierarchical-polygon-policy-v1",
            "aipackaging.graph": "encoder" if name == "encoder.onnx" else "placement-head",
            "aipackaging.opset": "23",
        }
        if {item.key: item.value for item in model.metadata_props} != expected_properties:
            raise ValueError(f"свойства графа ONNX не совпадают с контрактом: {name}")
        if any(item.data_location == onnx.TensorProto.EXTERNAL or item.external_data for item in model.graph.initializer):
            raise ValueError(f"внешние файлы весов ONNX запрещены: {name}")
    return metadata


def export_polygon_policy_bundle(
    checkpoint: str | Path,
    config_path: str | Path,
    output: str | Path,
    *,
    model_id: str = "hierarchical-polygon-policy-v1",
) -> dict[str, Any]:
    """Экспортирует выбранную контрольную точку и записывает метаданные v2."""

    config = load_polygon_training_config(config_path)
    destination = Path(output)
    destination.mkdir(parents=True, exist_ok=True)
    hidden = config["model"]["hiddenSize"]
    model = HierarchicalPolygonPolicyV1(hidden)
    checkpoint_hash = sha256_file(checkpoint)
    load_polygon_checkpoint(checkpoint, model, torch.device("cpu"), expected_sha256=checkpoint_hash)
    model.eval()
    encoder_path = destination / "encoder.onnx"
    head_path = destination / "placement-head.onnx"
    instances = torch.export.Dim("instances", min=1, max=config["model"]["maxInstances"])
    candidates = torch.export.Dim("candidates", min=1, max=config["model"]["maxCandidatesPerPair"])
    _export_onnx(
        PolygonEncoderExport(model.encoder).eval(),
        (
            torch.zeros((2, 128, 128), dtype=torch.float32),
            torch.zeros((2, 4, 32, 32), dtype=torch.float32),
            torch.zeros((2, 7), dtype=torch.float32),
            torch.zeros((7,), dtype=torch.float32),
        ),
        encoder_path,
        input_names=["sheet", "part_masks", "part_features", "objective"],
        output_names=["state_embedding", "orientation_embeddings", "instance_logits", "rotation_logits", "value"],
        dynamic_shapes=(None, {0: instances}, {0: instances}, None),
        opset_version=23,
        dynamo=True,
        external_data=False,
        verbose=False,
    )
    _stamp_graph(encoder_path, "encoder")
    _export_onnx(
        model.placement_head.eval(),
        (
            torch.zeros((hidden,), dtype=torch.float32),
            torch.zeros((hidden,), dtype=torch.float32),
            torch.zeros((4, 128, 128), dtype=torch.float32),
            torch.zeros((2, 7), dtype=torch.float32),
        ),
        head_path,
        input_names=["state_embedding", "orientation_embedding", "raster", "candidate_features"],
        output_names=["position_logits"],
        dynamic_shapes=(None, None, None, {0: candidates}),
        opset_version=23,
        dynamo=True,
        external_data=False,
        verbose=False,
    )
    _stamp_graph(head_path, "placement-head")
    files = {encoder_path.name: sha256_file(encoder_path), head_path.name: sha256_file(head_path)}
    metadata: dict[str, Any] = {
        "format": "aipackaging.polygon_policy",
        "version": 2,
        "modelId": model_id,
        "projectVersion": __version__,
        "architecture": "hierarchical-polygon-policy-v1",
        "opset": 23,
        "contracts": {"problem": 1, "solution": 2, "observation": 2, "action": 1, "reward": 2},
        "limits": {
            "hiddenSize": hidden,
            "sheetRasterSize": 128,
            "shapeRasterSize": 32,
            "maxInstances": config["model"]["maxInstances"],
            "maxCandidatesPerPair": config["model"]["maxCandidatesPerPair"],
        },
        "normalization": "polygon-observation-v2",
        "selection": {
            "hierarchy": ["instance", "rotation", "position"],
            "tieBreak": "lowest-current-catalog-index",
            "sampling": "splitmix64-categorical-v1",
        },
        "dynamicAxes": ["instances", "candidates"],
        "datasetManifestSha256": config["datasetManifestSha256"],
        "trainingConfigSha256": sha256_file(config_path),
        "checkpointSha256": checkpoint_hash,
        "files": files,
        "modelSha256": bundle_digest(files),
    }
    write_canonical_json(destination / "metadata.json", metadata)
    return metadata
