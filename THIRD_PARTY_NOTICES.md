<!-- SPDX-License-Identifier: Apache-2.0 -->
# Third-party notices

This file is **regenerated before each release** by `tools/ci/third_party_report.py` from the packages actually installed in the built image. The table below is the intended dependency set (see `docs/LICENSING.md` §6). Verify each licence against the installed package metadata.

| Component | Version | Licence | Source |
|---|---|---|---|
| GStreamer / GLib / GObject | from base image | LGPL-2.1-or-later | https://gstreamer.freedesktop.org |
| json-glib | from apt | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/json-glib |
| libsoup 3 | from apt | LGPL-2.0-or-later | https://gitlab.gnome.org/GNOME/libsoup |
| librdkafka | from apt | BSD-2-Clause | https://github.com/confluentinc/librdkafka |
| PyYAML | pip | MIT | https://pyyaml.org |
| Jinja2 | pip | BSD-3-Clause | https://palletsprojects.com/p/jinja |
| jsonschema | pip | MIT | https://github.com/python-jsonschema/jsonschema |
| onnx | pip | Apache-2.0 | https://github.com/onnx/onnx |
| *Transitive Python dependencies (installed by pip for the packages above; versions as resolved on 2026-11-20)* | | | |
| MarkupSafe (Jinja2) | 3.0.4 | BSD-3-Clause | https://github.com/pallets/markupsafe |
| attrs, referencing, rpds-py, jsonschema-specifications (jsonschema) | 26.1.0 / 0.37.0 / 2026.9.1 / 2025.9.1 | MIT | https://github.com/python-jsonschema |
| numpy (onnx) | 2.5.5 | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 † | https://numpy.org |
| protobuf (onnx) | 7.36.2 | BSD-3-Clause | https://github.com/protocolbuffers/protobuf |
| ml_dtypes (onnx) | 0.7.0 | Apache-2.0 | https://github.com/jax-ml/ml_dtypes |
| typing_extensions (onnx) | 4.16.0 | PSF-2.0 † | https://github.com/python/typing_extensions |
| NVIDIA DeepStream SDK, TensorRT, CUDA | base image | NVIDIA proprietary (EULA) | https://developer.nvidia.com/deepstream-sdk |

† Licence id added to the allow-list by the project owner on 2026-11-20 (`docs/LICENSING.md` §6).

Not distributed by this project: model weights, DeepStream sample models/streams, Ultralytics software.
