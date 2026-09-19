"""Проверяет переносимый выбор и синтетический комплект полигональной модели."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from aipackaging_ml import _aipackaging_solver as _native
from aipackaging_ml.datasets.serialization import canonical_json
from aipackaging_ml.polygon_exporting import load_polygon_model_metadata
from aipackaging_ml.polygon_onnx import OnnxPolygonPolicy, OnnxPolygonPolicyRunner
from aipackaging_ml.polygon_sampling import SplitMix64, rollout_seed, select_logit


FIXTURES = Path(__file__).parent / "fixtures"
MODEL = Path(__file__).parents[2] / "tests" / "fixtures" / "models" / "polygon-policy-smoke"
PROBLEM = FIXTURES / "polygon-smoke-problem.json"


def test_splitmix64_and_stable_selection() -> None:
    """Последовательность фиксирована, а жадное равенство выбирает меньший индекс."""

    generator = SplitMix64(42)
    assert [generator.next_u64() for _ in range(3)] == [
        13679457532755275413,
        2949826092126892291,
        5139283748462763858,
    ]
    assert rollout_seed(42, 0) == 13679457532755275413
    assert select_logit([1.0, 3.0, 3.0], [True, True, True], None) == 1
    assert select_logit([100.0, 2.0], [False, True], None) == 1


def test_selection_rejects_invalid_values() -> None:
    """Выбор отклоняет несовпадающую маску, пустой уровень и NaN."""

    with pytest.raises(ValueError, match="разные размеры"):
        select_logit([1.0], [], None)
    with pytest.raises(ValueError, match="не содержит"):
        select_logit([1.0], [False], None)
    with pytest.raises(ValueError, match="нечисловую"):
        select_logit([float("nan")], [True], None)


def test_synthetic_bundle_is_strict_and_solves() -> None:
    """Синтетические графы поддерживают динамические формы и точный валидатор."""

    metadata = load_polygon_model_metadata(MODEL)
    schema = json.loads((Path(__file__).parents[2] / "schemas" / "polygon-policy-v2.schema.json").read_text(encoding="utf-8"))
    Draft202012Validator(schema).validate(metadata)
    assert metadata["modelId"] == "synthetic-polygon-policy-smoke"
    problem = json.loads(PROBLEM.read_text(encoding="utf-8"))
    runner = OnnxPolygonPolicyRunner(OnnxPolygonPolicy(MODEL), revision="test")
    greedy = runner.solve(problem, mode="greedy", seed=42)
    repeated = runner.solve(problem, mode="greedy", seed=42)
    assert greedy["placements"] == repeated["placements"]
    assert not _native.validate_polygon_solution(canonical_json(problem), canonical_json(greedy))
    hybrid = runner.solve(problem, mode="hybrid", rollouts=2, seed=42)
    assert hybrid["solver"]["family"] == "hybrid"
    assert not _native.validate_polygon_solution(canonical_json(problem), canonical_json(hybrid))
    sampled = runner.solve(problem, mode="best-of", rollouts=1, seed=42)
    assert sampled["placements"] == [
        {"partId": "rectangle", "instanceIndex": 0, "xMicrometers": 2000,
         "yMicrometers": 28000, "rotationDegrees": 90},
        {"partId": "rectangle", "instanceIndex": 1, "xMicrometers": 23000,
         "yMicrometers": 2000, "rotationDegrees": 90},
    ]


def test_bundle_rejects_changed_graph(tmp_path: Path) -> None:
    """Изменение любого графа обнаруживается до создания сеанса."""

    destination = tmp_path / "model"
    destination.mkdir()
    for source in MODEL.iterdir():
        (destination / source.name).write_bytes(source.read_bytes())
    with (destination / "encoder.onnx").open("ab") as stream:
        stream.write(b"damage")
    with pytest.raises(ValueError, match="контрольная сумма"):
        load_polygon_model_metadata(destination)


def test_bundle_rejects_unknown_metadata_field(tmp_path: Path) -> None:
    """Строгий загрузчик отклоняет неизвестные поля метаданных."""

    destination = tmp_path / "model"
    destination.mkdir()
    for source in MODEL.iterdir():
        (destination / source.name).write_bytes(source.read_bytes())
    metadata_path = destination / "metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    metadata["unknown"] = True
    metadata_path.write_text(json.dumps(metadata), encoding="utf-8")
    with pytest.raises(ValueError, match="нестрогие"):
        load_polygon_model_metadata(destination)


def test_bundle_rejects_unexpected_file(tmp_path: Path) -> None:
    """Комплект не может скрывать дополнительные файлы рядом с графами."""

    destination = tmp_path / "model"
    destination.mkdir()
    for source in MODEL.iterdir():
        (destination / source.name).write_bytes(source.read_bytes())
    (destination / "unexpected.txt").write_text("unexpected", encoding="utf-8")
    with pytest.raises(ValueError, match="ровно три файла"):
        load_polygon_model_metadata(destination)
