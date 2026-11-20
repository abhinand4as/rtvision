# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Shared fixtures for the rtvision_tools tests."""

from __future__ import annotations

from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
EXAMPLES = REPO / "models" / "examples"
# Models used only by tests (e.g. a non-permissive licence for the licence gate).
FIXTURE_MODELS = REPO / "tests" / "python" / "fixtures" / "models"
EXAMPLE_CONFIG = REPO / "config" / "rtvision.example.yaml"
GOLDEN = REPO / "tests" / "golden"


@pytest.fixture
def repo() -> Path:
    return REPO


def write_model(tmp_path: Path, model_yaml: str, labels: list[str] | None = None) -> Path:
    """Writes a model dir (model.yaml + labels.txt) and returns the model.yaml path."""
    d = tmp_path / "models" / "m"
    d.mkdir(parents=True, exist_ok=True)
    (d / "model.yaml").write_text(model_yaml)
    if labels is not None:
        (d / "labels.txt").write_text("\n".join(labels) + "\n")
    return d / "model.yaml"
