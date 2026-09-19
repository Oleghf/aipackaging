"""Проверяет общий корпус через JSON Schema и нативную привязку C++."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from jsonschema import Draft202012Validator

from aipackaging_ml import _aipackaging_solver as native


ROOT = Path(__file__).resolve().parents[2]
CORPUS = ROOT / "tests/contracts"


def _read_json(path: Path) -> dict:
    """Читает один тестовый пример JSON без преобразования значений."""

    return json.loads(path.read_text(encoding="utf-8"))


def _wire(document: dict) -> str:
    """Сериализует тестовый пример в стабильный JSON для нативного API."""

    return json.dumps(document, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _native_accepts(item: dict, document: dict) -> bool:
    """Возвращает итог синтаксического и доменного анализа C++ для записи манифеста."""

    contract = item["contract"]
    try:
        if contract == "grid_problem":
            native.create_environment(_wire(document))
            return True
        if contract == "polygon_problem":
            native.create_polygon_environment(_wire(document))
            return True

        problem = _read_json(CORPUS / item["problem"])
        if contract == "grid_solution":
            return native.validate_solution(_wire(problem), _wire(document)) == ""
        if contract == "polygon_solution":
            return native.validate_polygon_solution(_wire(problem), _wire(document)) == ""
    except (RuntimeError, ValueError):
        return False
    raise AssertionError(f"неизвестный контракт: {contract}")


def test_schema_and_native_classification_match_manifest() -> None:
    """Каждый тестовый пример должен одинаково классифицироваться всеми слоями."""

    manifest = _read_json(CORPUS / "manifest.json")
    assert manifest["format"] == "aipackaging.contract_corpus"
    assert manifest["version"] == 1

    for item in manifest["cases"]:
        document = _read_json(CORPUS / item["file"])
        schema = _read_json(CORPUS / item["schema"])
        assert Draft202012Validator(schema).is_valid(document) is item["expectedSchema"], item["id"]
        assert _native_accepts(item, document) is item["expectedDomain"], item["id"]


def test_grid_validator_rejects_extreme_placement_without_crashing() -> None:
    """Повреждённая координата решения должна дать ошибку, а не завершить процесс."""

    problem = _read_json(CORPUS / "grid/problem-valid.json")
    solution = _read_json(CORPUS / "grid/solution-valid-v1.json")
    for field in ("column", "row"):
        for value in (-(2**31), 2**31 - 1):
            solution["placements"][0][field] = value
            assert native.validate_solution(_wire(problem), _wire(solution))
        solution["placements"][0][field] = 0


def test_polygon_parser_rejects_extent_overflow_before_search() -> None:
    """Большой размах микронных координат отклоняется как невалидная геометрия."""

    problem = _read_json(CORPUS / "polygon/problem-valid.json")
    outer = problem["parts"][0]["outer"]
    outer["start"]["x"] = -9e15
    for segment in outer["segments"]:
        segment["end"]["x"] = -9e15 if segment["end"]["x"] == 0 else 9e15
    with pytest.raises(ValueError, match="extent"):
        native.create_polygon_environment(_wire(problem))
