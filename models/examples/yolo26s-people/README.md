<!-- SPDX-License-Identifier: Apache-2.0 -->
# yolo26s-people

**Licence warning:** Ultralytics YOLO26 weights, and models you train with Ultralytics, are licensed **AGPL-3.0** by Ultralytics. Closed-source or commercial deployment requires an Ultralytics Enterprise licence. Perceptra blocks this model unless you set `PERCEPTRA_ACCEPT_MODEL_LICENSE=AGPL-3.0-only`.

## Produce the files (outside the Perceptra image)

```bash
python -m venv .venv && . .venv/bin/activate
pip install ultralytics onnx onnxslim        # AGPL-3.0 tooling, installed by you, not by Perceptra
python - <<'PY'
from ultralytics import YOLO
m = YOLO("yolo26s.pt")
m.export(format="onnx", imgsz=640, dynamic=True, simplify=True)   # end-to-end (NMS-free) head
open("labels.txt", "w").write("\n".join(m.names[i] for i in range(len(m.names))) + "\n")
PY
cp yolo26s.onnx labels.txt /opt/perceptra/models/yolo26s-people/
```

Check the output with `python -m perceptra_tools.onnx_inspect yolo26s.onnx`. Expected: one output, `[batch, 300, 6]` → family `yolo_e2e`. If you exported with `end2end=False`, the output is `[batch, 84, 8400]`: set `output.family: yolo_raw` and `classes.nms_iou`.
