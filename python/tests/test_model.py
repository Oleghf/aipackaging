"""Изолированные тесты PyTorch-архитектуры без нативной среды."""

from __future__ import annotations

import pytest

torch = pytest.importorskip("torch")

from aipackaging_ml.model import HierarchicalGridPolicyV1


def test_hierarchical_model_supports_variable_instance_and_candidate_counts() -> None:
    """Encoder и positional head сохраняют документированные динамические оси."""

    model = HierarchicalGridPolicyV1(hidden_size=32)
    encoded = model.encode(
        torch.zeros((8, 9)),
        torch.zeros((3, 4, 3, 2)),
        torch.zeros((3, 7)),
        torch.zeros(7),
    )
    assert encoded.state_embedding.shape == (32,)
    assert encoded.orientation_embeddings.shape == (3, 4, 32)
    assert encoded.instance_logits.shape == (3,)
    assert encoded.rotation_logits.shape == (3, 4)
    assert encoded.value.shape == ()
    assert model.positions(encoded.state_embedding, encoded.orientation_embeddings[1, 2], torch.zeros((17, 7))).shape == (17,)


def test_hierarchical_model_rejects_incompatible_hidden_width() -> None:
    """Нечётная ширина отклоняется до создания несовместимых Linear-слоёв."""

    with pytest.raises(ValueError, match="even integer"):
        HierarchicalGridPolicyV1(hidden_size=31)
