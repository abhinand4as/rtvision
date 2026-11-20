<!-- SPDX-License-Identifier: Apache-2.0 -->
# Optional: Ultralytics YOLO exporter

**This helper is optional, installed separately, and never a dependency of the Perceptra service.**

`export_yolo.py` is our own small script that calls *your* installed `ultralytics` package to export
a YOLO model (e.g. YOLO26) to ONNX for the `yolo_e2e` or `yolo_raw` output family. No Ultralytics
code is copied into this repository.

**Licence warning:** the `ultralytics` package and the models it produces (including YOLO26 weights
and models you train with it) are licensed **AGPL-3.0** by Ultralytics unless you hold an
Ultralytics Enterprise licence. Installing and running it is your decision; see
[docs/LICENSING.md](../../../docs/LICENSING.md) §5.

Status: placeholder (script implemented together with the YOLO26 profile in M7).
