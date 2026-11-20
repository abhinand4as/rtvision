# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""onnx_inspect: tiny synthetic graphs built with onnx.helper (no downloaded models)."""

from __future__ import annotations

from pathlib import Path

import onnx
import pytest
from onnx import TensorProto, helper
from perceptra_tools import onnx_inspect

Shape = list[int | str]


def make_onnx(
    path: Path, outputs: list[tuple[str, Shape, int]], inputs: list[tuple[str, Shape]] | None = None
) -> Path:
    ins = [
        helper.make_tensor_value_info(n, TensorProto.FLOAT, s)
        for n, s in (inputs or [("images", ["batch", 3, 640, 640])])
    ]
    outs = [helper.make_tensor_value_info(n, t, s) for n, s, t in outputs]
    graph = helper.make_graph([], "g", ins, outs)
    onnx.save(helper.make_model(graph), str(path))
    return path


F, I32 = TensorProto.FLOAT, TensorProto.INT32
CASES = {
    "yolo_e2e": [("output0", ["batch", 300, 6], F)],
    "yolo_raw": [("output0", ["batch", 84, 8400], F)],
    "detr": [("boxes", ["batch", 300, 4], F), ("scores", ["batch", 300, 80], F)],
    "efficient_nms": [
        ("num_dets", ["batch", 1], I32),
        ("det_boxes", ["batch", 100, 4], F),
        ("det_scores", ["batch", 100], F),
        ("det_classes", ["batch", 100], I32),
    ],
}


@pytest.mark.parametrize("family", list(CASES))
def test_suggest_and_check_ok(tmp_path: Path, family: str) -> None:
    p = make_onnx(tmp_path / "m.onnx", CASES[family])
    inputs, outputs = onnx_inspect.load_io(p)
    assert onnx_inspect.suggest_family(outputs)[0] == family
    rep = onnx_inspect.check(
        inputs,
        outputs,
        family,
        80,
        [o[0] for o in CASES[family]],
        max_streams=4,
        input_wh=(640, 640),
    )
    assert rep.errors == [] and rep.warnings == []


def test_yolo_raw_transposed_layout_ok(tmp_path: Path) -> None:
    p = make_onnx(tmp_path / "m.onnx", [("output0", ["batch", 8400, 84], F)])
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), "yolo_raw", 80)
    assert rep.errors == []


@pytest.mark.parametrize(
    ("family", "outputs", "num_classes", "match"),
    [
        ("yolo_e2e", [("output0", ["b", 300, 7], F)], 80, r"\[K, 6\]"),
        ("yolo_raw", [("output0", ["b", 85, 8400], F)], 80, "4 \\+ num_classes = 84"),
        ("detr", [("boxes", ["b", 300, 4], F), ("scores", ["b", 300, 81], F)], 80, r"\[Q, 80\]"),
        ("detr", [("boxes", ["b", 300, 4], F), ("scores", ["b", 200, 80], F)], 80, "disagree"),
        ("efficient_nms", [("output0", ["b", 300, 6], F)], 80, "needs 4 output"),
    ],
)
def test_check_errors(tmp_path: Path, family, outputs, num_classes, match) -> None:
    p = make_onnx(tmp_path / "m.onnx", outputs)
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), family, num_classes)
    assert any(__import__("re").search(match, e) for e in rep.errors), rep.errors


def test_layers_must_exist(tmp_path: Path) -> None:
    p = make_onnx(tmp_path / "m.onnx", CASES["detr"])
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), "detr", 80, ["boxes", "logits"])
    assert "not found" in rep.errors[0]


def test_background_class_column(tmp_path: Path) -> None:
    p = make_onnx(tmp_path / "m.onnx", [("boxes", ["b", 300, 4], F), ("logits", ["b", 300, 81], F)])
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), "detr", 80, has_background_class=True)
    assert rep.errors == []


def test_fixed_batch_warning_and_input_checks(tmp_path: Path) -> None:
    p = make_onnx(tmp_path / "m.onnx", CASES["yolo_e2e"], inputs=[("images", [1, 3, 640, 640])])
    rep = onnx_inspect.check(
        *onnx_inspect.load_io(p), "yolo_e2e", 80, max_streams=4, input_wh=(640, 640)
    )
    assert rep.errors == [] and "fixed batch of 1 < service.max_streams 4" in rep.warnings[0]
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), "yolo_e2e", 80, input_wh=(960, 544))
    assert "640x640 but model.yaml input is 960x544" in rep.errors[0]
    p = make_onnx(
        tmp_path / "m2.onnx",
        CASES["detr"],
        inputs=[("images", ["b", 3, 640, 640]), ("orig_target_sizes", ["b", 2])],
    )
    rep = onnx_inspect.check(*onnx_inspect.load_io(p), "detr", 80)
    assert "exactly one image input" in rep.errors[0]


def test_cli(tmp_path: Path, capsys) -> None:
    p = make_onnx(tmp_path / "m.onnx", CASES["yolo_e2e"])
    assert onnx_inspect.main([str(p)]) == 0
    out = capsys.readouterr().out
    assert "suggested family: yolo_e2e" in out and "output0 [batch, 300, 6] FLOAT" in out
    assert onnx_inspect.main([str(p), "--family", "yolo_raw", "--num-classes", "80"]) == 1
    assert onnx_inspect.main([str(tmp_path / "missing.onnx")]) == 2
