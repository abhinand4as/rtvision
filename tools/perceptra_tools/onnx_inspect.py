# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Inspect ONNX input/output names and shapes and check them against an output family.

No inference is run and external weight data is not loaded. Usage::

    python -m perceptra_tools.onnx_inspect model.onnx
    python -m perceptra_tools.onnx_inspect model.onnx --family detr --num-classes 80 \\
        --layers boxes,scores --max-streams 4

Shapes are reported as written in the graph; symbolic dims (e.g. ``batch``) are kept as names.
The first dim of every tensor is treated as the batch dim (nvinfer uses explicit batch).
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

Dim = int | str | None

FAMILIES = ("yolo_e2e", "yolo_raw", "detr", "efficient_nms")
EXPECTED_OUTPUTS = {"yolo_e2e": 1, "yolo_raw": 1, "detr": 2, "efficient_nms": 4}


@dataclass
class TensorInfo:
    name: str
    shape: list[Dim]
    dtype: str

    def per_frame(self) -> list[Dim]:
        """Shape without the batch dim."""
        return self.shape[1:]

    def fmt(self) -> str:
        dims = ", ".join("?" if d is None else str(d) for d in self.shape)
        return f"{self.name} [{dims}] {self.dtype}"


@dataclass
class Report:
    inputs: list[TensorInfo]
    outputs: list[TensorInfo]
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def _dims(type_proto) -> list[Dim]:
    out: list[Dim] = []
    for d in type_proto.tensor_type.shape.dim:
        if d.HasField("dim_value"):
            out.append(int(d.dim_value))
        elif d.HasField("dim_param"):
            out.append(d.dim_param)
        else:
            out.append(None)
    return out


def load_io(path: Path) -> tuple[list[TensorInfo], list[TensorInfo]]:
    """Graph inputs (excluding initializers) and outputs."""
    import onnx  # imported lazily: only needed when a model is inspected

    model = onnx.load(str(path), load_external_data=False)
    graph = model.graph
    initializers = {i.name for i in graph.initializer}

    def info(v) -> TensorInfo:
        elem = v.type.tensor_type.elem_type
        return TensorInfo(v.name, _dims(v.type), onnx.TensorProto.DataType.Name(elem))

    inputs = [info(v) for v in graph.input if v.name not in initializers]
    outputs = [info(v) for v in graph.output]
    return inputs, outputs


def _known(d: Dim) -> bool:
    return isinstance(d, int) and d > 0


def suggest_family(outputs: list[TensorInfo]) -> tuple[str | None, str]:
    """Best-effort family guess from output shapes. Returns (family or None, reason)."""
    names = [o.name.lower() for o in outputs]
    if len(outputs) == 4 and any("num" in n and "det" in n for n in names):
        return "efficient_nms", "4 outputs including a num_dets tensor (EfficientNMS plugin)"
    if len(outputs) == 2:
        lasts = [o.per_frame()[-1] if o.per_frame() else None for o in outputs]
        if 4 in lasts:
            return "detr", "2 outputs, one with last dim 4 (boxes [Q, 4] + logits [Q, C])"
    if len(outputs) == 1:
        pf = outputs[0].per_frame()
        if len(pf) == 2 and pf[1] == 6:
            return "yolo_e2e", "1 output [K, 6] (x1, y1, x2, y2, score, class)"
        if len(pf) == 2 and (_known(pf[0]) or _known(pf[1])):
            return "yolo_raw", "1 output [4 + C, A] or [A, 4 + C]"
    return None, "no known family matches the output shapes"


def check(
    inputs: list[TensorInfo],
    outputs: list[TensorInfo],
    family: str,
    num_classes: int,
    layers: list[str] | None = None,
    has_background_class: bool = False,
    max_streams: int | None = None,
    input_wh: tuple[int, int] | None = None,
) -> Report:
    """Checks a model against the declared family. Errors block startup; warnings do not."""
    rep = Report(inputs, outputs)
    by_name = {o.name: o for o in outputs}

    if len(inputs) != 1:
        rep.errors.append(
            f"model has {len(inputs)} inputs ({', '.join(i.name for i in inputs)}); nvinfer "
            "feeds exactly one image input (export without extra inputs / post-processor)"
        )
    if inputs:
        shape = inputs[0].shape
        if len(shape) != 4:
            rep.errors.append(f"input '{inputs[0].name}' must be [B, C, H, W], got {shape}")
        else:
            batch, _c, h, w = shape
            if input_wh and _known(h) and _known(w) and (w, h) != input_wh:
                rep.errors.append(
                    f"input '{inputs[0].name}' is {w}x{h} but model.yaml input is "
                    f"{input_wh[0]}x{input_wh[1]}"
                )
            if max_streams and _known(batch) and batch < max_streams:
                rep.warnings.append(
                    f"input '{inputs[0].name}' has a fixed batch of {batch} < "
                    f"service.max_streams {max_streams}: export with a dynamic batch dim or "
                    f"lower max_streams"
                )

    if family not in FAMILIES:
        rep.errors.append(f"unknown family '{family}'")
        return rep

    need = EXPECTED_OUTPUTS[family]
    if layers:
        missing = [n for n in layers if n not in by_name]
        if missing:
            rep.errors.append(
                f"output.layers {missing} not found; model outputs are {[o.name for o in outputs]}"
            )
            return rep
        if len(layers) != need:
            rep.errors.append(
                f"family {family} needs {need} layer(s), output.layers has {len(layers)}"
            )
            return rep
        selected = [by_name[n] for n in layers]
    else:
        if len(outputs) < need:
            rep.errors.append(f"family {family} needs {need} output(s), model has {len(outputs)}")
            return rep
        selected = outputs[:need]
        if len(outputs) > need:
            rep.warnings.append(
                f"model has {len(outputs)} outputs; without output.layers the first {need} "
                f"are used: {[o.name for o in selected]}"
            )

    shapes = [o.per_frame() for o in selected]

    def err(msg: str) -> None:
        rep.errors.append(f"{family}: {msg}")

    if family == "yolo_e2e":
        s = shapes[0]
        if len(s) != 2 or (s[1] is not None and s[1] != 6 and not isinstance(s[1], str)):
            err(f"expected [K, 6] per frame, got {s}")
    elif family == "yolo_raw":
        s = shapes[0]
        ch = 4 + num_classes
        if len(s) != 2:
            err(f"expected [4 + C, A] or [A, 4 + C] per frame, got {s}")
        elif all(_known(d) for d in s) and ch not in s:
            err(
                f"no dim equals 4 + num_classes = {ch} in {s} (YOLOv5-style objectness unsupported)"
            )
    elif family == "detr":
        boxes, logits = shapes
        if len(boxes) == 2 and len(logits) == 2 and boxes[-1] != 4 and logits[-1] == 4:
            boxes, logits = logits, boxes
            rep.warnings.append("detr: output order looks like (logits, boxes); list boxes first")
        cols = num_classes + (1 if has_background_class else 0)
        if len(boxes) != 2 or (_known(boxes[-1]) and boxes[-1] != 4):
            err(f"boxes must be [Q, 4] per frame, got {boxes}")
        if len(logits) != 2 or (_known(logits[-1]) and logits[-1] != cols):
            err(
                f"logits must be [Q, {cols}] per frame (num_classes={num_classes}, "
                f"has_background_class={has_background_class}), got {logits}"
            )
        if len(boxes) == 2 and len(logits) == 2 and _known(boxes[0]) and _known(logits[0]):
            if boxes[0] != logits[0]:
                err(
                    f"boxes and logits disagree on the number of queries: {boxes[0]} vs {logits[0]}"
                )
    elif family == "efficient_nms":
        nd, bx, sc, cl = shapes
        if len(bx) != 2 or (_known(bx[-1]) and bx[-1] != 4):
            err(f"boxes must be [M, 4] per frame, got {bx}")
        if len(sc) != 1 or len(cl) != 1:
            err(f"scores and classes must be [M] per frame, got {sc} and {cl}")
        if len(nd) > 1 or (nd and _known(nd[0]) and nd[0] != 1):
            err(f"num_dets must have 1 element per frame, got {nd}")
    return rep


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("onnx", type=Path)
    ap.add_argument("--family", choices=FAMILIES)
    ap.add_argument("--num-classes", type=int)
    ap.add_argument("--layers", help="comma-separated output layer names")
    ap.add_argument("--has-background-class", action="store_true")
    ap.add_argument("--max-streams", type=int)
    args = ap.parse_args(argv)

    try:
        inputs, outputs = load_io(args.onnx)
    except Exception as exc:  # noqa: BLE001 - report any load failure to the user
        print(f"error: cannot read {args.onnx}: {exc}", file=sys.stderr)
        return 2

    print("inputs:")
    for t in inputs:
        print(f"  {t.fmt()}")
    print("outputs:")
    for t in outputs:
        print(f"  {t.fmt()}")
    family, reason = suggest_family(outputs)
    print(f"suggested family: {family or 'unknown'} ({reason})")

    if not args.family:
        return 0
    if args.num_classes is None:
        print("error: --num-classes is required with --family", file=sys.stderr)
        return 2
    layers = [x.strip() for x in args.layers.split(",")] if args.layers else None
    rep = check(
        inputs,
        outputs,
        args.family,
        args.num_classes,
        layers,
        args.has_background_class,
        args.max_streams,
    )
    for w in rep.warnings:
        print(f"warning: {w}")
    for e in rep.errors:
        print(f"error: {e}")
    print("OK" if not rep.errors else "FAILED")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
