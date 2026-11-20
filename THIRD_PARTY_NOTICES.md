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
| NVIDIA DeepStream SDK, TensorRT, CUDA | base image | NVIDIA proprietary (EULA) | https://developer.nvidia.com/deepstream-sdk |

Not distributed by this project: model weights, DeepStream sample models/streams, Ultralytics software.
