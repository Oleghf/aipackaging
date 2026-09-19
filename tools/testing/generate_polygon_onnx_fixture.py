"""Создаёт малый синтетический комплект ONNX для автоматических тестов."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import onnx
from onnx import TensorProto, helper


ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "tests" / "fixtures" / "models" / "polygon-policy-smoke"


def _zero_shape(name: str, source: str, suffix: list[int], nodes: list[onnx.NodeProto]) -> None:
    """Добавляет вычисление динамической формы и заполненного нулями тензора."""

    shape = f"{name}_source_shape"
    count = f"{name}_count"
    count_vector = f"{name}_count_vector"
    result_shape = f"{name}_shape"
    nodes.extend(
        [
            helper.make_node("Shape", [source], [shape]),
            helper.make_node("Gather", [shape, "index_zero"], [count], axis=0),
            helper.make_node("Unsqueeze", [count, "axes_zero"], [count_vector]),
            helper.make_node("Concat", [count_vector, f"{name}_suffix"], [result_shape], axis=0),
            helper.make_node("ConstantOfShape", [result_shape], [name], value=helper.make_tensor("zero", TensorProto.FLOAT, [1], [0.0])),
        ]
    )


def _encoder() -> onnx.ModelProto:
    """Строит граф с корректными динамическими формами и нулевыми оценками."""

    nodes: list[onnx.NodeProto] = []
    _zero_shape("orientation_embeddings", "part_features", [4, 16], nodes)
    _zero_shape("instance_logits", "part_features", [], nodes)
    _zero_shape("rotation_logits", "part_features", [4], nodes)
    nodes.extend(
        [
            helper.make_node("Identity", ["state_zero"], ["state_embedding"]),
            helper.make_node("Identity", ["value_zero"], ["value"]),
        ]
    )
    graph = helper.make_graph(
        nodes,
        "synthetic-polygon-encoder",
        [
            helper.make_tensor_value_info("sheet", TensorProto.FLOAT, [2, 128, 128]),
            helper.make_tensor_value_info("part_masks", TensorProto.FLOAT, ["instances", 4, 32, 32]),
            helper.make_tensor_value_info("part_features", TensorProto.FLOAT, ["instances", 7]),
            helper.make_tensor_value_info("objective", TensorProto.FLOAT, [7]),
        ],
        [
            helper.make_tensor_value_info("state_embedding", TensorProto.FLOAT, [16]),
            helper.make_tensor_value_info("orientation_embeddings", TensorProto.FLOAT, ["instances", 4, 16]),
            helper.make_tensor_value_info("instance_logits", TensorProto.FLOAT, ["instances"]),
            helper.make_tensor_value_info("rotation_logits", TensorProto.FLOAT, ["instances", 4]),
            helper.make_tensor_value_info("value", TensorProto.FLOAT, []),
        ],
        initializer=[
            helper.make_tensor("index_zero", TensorProto.INT64, [], [0]),
            helper.make_tensor("axes_zero", TensorProto.INT64, [1], [0]),
            helper.make_tensor("orientation_embeddings_suffix", TensorProto.INT64, [2], [4, 16]),
            helper.make_tensor("instance_logits_suffix", TensorProto.INT64, [0], []),
            helper.make_tensor("rotation_logits_suffix", TensorProto.INT64, [1], [4]),
            helper.make_tensor("state_zero", TensorProto.FLOAT, [16], [0.0] * 16),
            helper.make_tensor("value_zero", TensorProto.FLOAT, [], [0.0]),
        ],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 23)])
    model.ir_version = 10
    onnx.checker.check_model(model)
    return model


def _placement_head() -> onnx.ModelProto:
    """Строит позиционную голову с переменным числом нулевых оценок."""

    nodes: list[onnx.NodeProto] = []
    _zero_shape("position_logits", "candidate_features", [], nodes)
    graph = helper.make_graph(
        nodes,
        "synthetic-polygon-placement-head",
        [
            helper.make_tensor_value_info("state_embedding", TensorProto.FLOAT, [16]),
            helper.make_tensor_value_info("orientation_embedding", TensorProto.FLOAT, [16]),
            helper.make_tensor_value_info("raster", TensorProto.FLOAT, [4, 128, 128]),
            helper.make_tensor_value_info("candidate_features", TensorProto.FLOAT, ["candidates", 7]),
        ],
        [helper.make_tensor_value_info("position_logits", TensorProto.FLOAT, ["candidates"])],
        initializer=[
            helper.make_tensor("index_zero", TensorProto.INT64, [], [0]),
            helper.make_tensor("axes_zero", TensorProto.INT64, [1], [0]),
            helper.make_tensor("position_logits_suffix", TensorProto.INT64, [0], []),
        ],
    )
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 23)])
    model.ir_version = 10
    onnx.checker.check_model(model)
    return model


def _sha256(path: Path) -> str:
    """Возвращает SHA-256 указанного файла."""

    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    """Записывает оба графа и согласованные строгие метаданные v2."""

    OUTPUT.mkdir(parents=True, exist_ok=True)
    encoder = _encoder()
    placement = _placement_head()
    helper.set_model_props(
        encoder,
        {"aipackaging.architecture": "hierarchical-polygon-policy-v1", "aipackaging.graph": "encoder", "aipackaging.opset": "23"},
    )
    helper.set_model_props(
        placement,
        {"aipackaging.architecture": "hierarchical-polygon-policy-v1", "aipackaging.graph": "placement-head", "aipackaging.opset": "23"},
    )
    onnx.save(encoder, OUTPUT / "encoder.onnx")
    onnx.save(placement, OUTPUT / "placement-head.onnx")
    files = {name: _sha256(OUTPUT / name) for name in ("encoder.onnx", "placement-head.onnx")}
    model_hash = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    metadata = {
        "format": "aipackaging.polygon_policy",
        "version": 2,
        "modelId": "synthetic-polygon-policy-smoke",
        "projectVersion": "0.8.0",
        "architecture": "hierarchical-polygon-policy-v1",
        "opset": 23,
        "contracts": {"problem": 1, "solution": 2, "observation": 2, "action": 1, "reward": 2},
        "limits": {"hiddenSize": 16, "sheetRasterSize": 128, "shapeRasterSize": 32, "maxInstances": 100, "maxCandidatesPerPair": 250000},
        "normalization": "polygon-observation-v2",
        "selection": {"hierarchy": ["instance", "rotation", "position"], "tieBreak": "lowest-current-catalog-index", "sampling": "splitmix64-categorical-v1"},
        "dynamicAxes": ["instances", "candidates"],
        "datasetManifestSha256": "0" * 64,
        "trainingConfigSha256": "1" * 64,
        "checkpointSha256": "2" * 64,
        "files": files,
        "modelSha256": model_hash,
    }
    (OUTPUT / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
