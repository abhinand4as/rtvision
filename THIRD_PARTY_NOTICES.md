<!-- SPDX-License-Identifier: Apache-2.0 -->
# Third-party notices

This file is **regenerated before each release** by `tools/ci/third_party_report.py` from the packages actually installed in the built image. The table below is the intended dependency set (see `docs/LICENSING.md` §6). Verify each licence against the installed package metadata.

| Component | Version | Licence | Source |
|---|---|---|---|
| GStreamer / GLib / GObject | from base image | LGPL-2.1-or-later | https://gstreamer.freedesktop.org |
| json-glib | base image (Dockerfile installs/pins it) | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/json-glib |
| libsoup 3 | base image (Dockerfile installs/pins it) | LGPL-2.0-or-later | https://gitlab.gnome.org/GNOME/libsoup |
| librdkafka | base image (Dockerfile installs/pins it) | BSD-2-Clause | https://github.com/confluentinc/librdkafka |
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

Used by the development/integration compose stack only (pulled from Docker Hub by the user, not part of the RTVision image):

| Component | Version | Licence | Source |
|---|---|---|---|
| Apache Kafka (`apache/kafka` image) | 4.3.2 | Apache-2.0 | https://kafka.apache.org |

Used on the host to export the default model (`tools/export/rfdetr`, own virtualenv, not part of the image; licences checked 2026-11-20):

| Component | Version | Licence | Source |
|---|---|---|---|
| rfdetr (and the RF-DETR Nano/Small/Medium/Large weights it downloads) | 1.12.1 | Apache-2.0 | https://github.com/roboflow/rf-detr |
| PyTorch / torchvision (CPU build) | 2.15.0 / 0.29.2 | BSD-3-Clause | https://pytorch.org |
| transformers, huggingface_hub, onnx | 5.23.0 / 2.4.0 / 1.23.2 | Apache-2.0 | https://github.com/huggingface |
| onnxruntime, supervision | 1.32.0 / 0.32.0 | MIT | https://onnxruntime.ai |
| certifi, tqdm (transitive) | | MPL-2.0 (file-level copyleft; unmodified) | https://github.com/certifi/python-certifi |

<!-- BEGIN GENERATED: tools/ci/third_party_report.py -->
## Generated report: contents of `rtvision:local` (image `9a649eb5a67f`)

Base image `nvcr.io/nvidia/deepstream:9.1-samples-multiarch`. Regenerate after any change to the Dockerfile or the Python dependencies: `python3 tools/ci/third_party_report.py --image rtvision:local --write`.

### Python packages in `/opt/rtvision/python` (14)

| Package | Version | Licence (package metadata) | URL |
|---|---|---|---|
| attrs | 26.1.0 | MIT | https://www.attrs.org/ |
| Jinja2 | 3.1.6 | BSD License | https://github.com/pallets/jinja/ |
| jsonschema | 4.26.0 | MIT | https://github.com/python-jsonschema/jsonschema |
| jsonschema-specifications | 2025.9.1 | MIT | https://github.com/python-jsonschema/jsonschema-specifications |
| MarkupSafe | 3.0.4 | BSD-3-Clause | https://github.com/pallets/markupsafe/ |
| ml_dtypes | 0.7.0 | Apache-2.0 | https://github.com/jax-ml/ml_dtypes |
| numpy | 2.5.5 | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 | https://numpy.org |
| onnx | 1.23.2 | Apache-2.0 | https://onnx.ai/ |
| protobuf | 7.36.2 | 3-Clause BSD License | https://developers.google.com/protocol-buffers/ |
| PyYAML | 6.0.4 | MIT | https://pyyaml.org/ |
| referencing | 0.37.0 | MIT | https://github.com/python-jsonschema/referencing |
| rpds-py | 2026.9.1 | MIT | https://github.com/crate-py/rpds |
| rtvision-tools | 0.1.0 | Apache-2.0 |  |
| typing_extensions | 4.16.0 | PSF-2.0 | https://github.com/python/typing_extensions |

### apt packages added by `docker/Dockerfile` on top of the base image (0)

| Package | Version | Licence | Basis |
|---|---|---|---|
| (none: every runtime package already ships in the base image) | | | |

### Shared libraries loaded by `rtvision` and `librtvision_parsers.so` (69)

Resolved with `ldd`. GStreamer plugins (including DeepStream's) are loaded at runtime from the base image and are not listed.

| Debian package | Source | Library licence | Basis | Libraries |
|---|---|---|---|---|
| cuda-cudart-13-2 | base image | NVIDIA CUDA EULA | reviewed: proprietary, from the base image | libcudart.so.13 |
| libblkid1 | base image | LGPL-2.1-or-later | reviewed: util-linux: libblkid is LGPL; GPL for the CLI tools | libblkid.so.1 |
| libbrotli1 | base image | MIT | copyright `Files: *` | libbrotlicommon.so.1, libbrotlidec.so.1 |
| libbz2-1.0 | base image | bzip2-1.0.6 (BSD-style) | reviewed: Files: * BSD-variant; GPL-2 only debian/* | libbz2.so.1.0 |
| libc6 | base image | LGPL-2.1-or-later | reviewed: GNU C Library (copyright file not machine-readable) | ld-linux-x86-64.so.2, libc.so.6, libdl.so.2, libm.so.6, libpthread.so.0, libresolv.so.2, librt.so.1 |
| libcom-err2 | base image | MIT-US-export | reviewed: e2fsprogs lib/et (com_err); GPL for the e2fs tools | libcom_err.so.2 |
| libdw1t64 | base image | LGPL-3.0-or-later OR GPL-2.0-or-later | reviewed: elfutils libraries (dual) | libdw.so.1 |
| libelf1t64 | base image | LGPL-3.0-or-later OR GPL-2.0-or-later | reviewed: elfutils libraries (dual) | libelf.so.1 |
| libffi8 | base image | MIT | reviewed: Expat; GPL only build/test scripts | libffi.so.8 |
| libgcc-s1 | base image | GPL-3.0-or-later WITH GCC-exception-3.1 | reviewed: GCC runtime library exception | libgcc_s.so.1 |
| libglib2.0-0t64 | base image | LGPL-2.1+ | copyright `Files: *`; others: 22 | libgio-2.0.so.0, libglib-2.0.so.0, libgmodule-2.0.so.0, libgobject-2.0.so.0 |
| libglvnd0 | base image | MIT | copyright `Files: *`; others: 5 | libGLdispatch.so |
| libgomp1 | base image | GPL-3.0-or-later WITH GCC-exception-3.1 | reviewed: GCC runtime; LD_PRELOADed by base | libgomp.so.1 |
| libgssapi-krb5-2 | base image | MIT | reviewed: MIT Kerberos | libgssapi_krb5.so.2 |
| libgstreamer-plugins-base1.0-0 | base image | LGPL-2.1-or-later | reviewed: gst-plugins-base libraries | libgstapp-1.0.so.0, libgstaudio-1.0.so.0, libgstpbutils-1.0.so.0, libgstrtp-1.0.so.0, libgstrtsp-1.0.so.0, libgstsdp-1.0.so.0, libgsttag-1.0.so.0, libgstvideo-1.0.so.0 |
| libgstreamer1.0-0 | base image | LGPL-2.1-or-later | reviewed: GStreamer core libraries | libgstbase-1.0.so.0, libgstnet-1.0.so.0, libgstreamer-1.0.so.0 |
| libgstrtspserver-1.0-0 | base image | LGPL-2.0-or-later | reviewed: gst-rtsp-server | libgstrtspserver-1.0.so.0 |
| libidn2-0 | base image | LGPL-3.0-or-later OR GPL-2.0-or-later | reviewed: *.c *.h (dual) | libidn2.so.0 |
| libjson-glib-1.0-0 | base image | LGPL-2.1+ | copyright `Files: *` | libjson-glib-1.0.so.0 |
| libk5crypto3 | base image | MIT | reviewed: MIT Kerberos | libk5crypto.so.3 |
| libkeyutils1 | base image | LGPL-2.0-or-later | reviewed: keyutils.* (the library) | libkeyutils.so.1 |
| libkrb5-3 | base image | MIT | reviewed: MIT Kerberos | libkrb5.so.3 |
| libkrb5support0 | base image | MIT | reviewed: MIT Kerberos | libkrb5support.so.0 |
| liblz4-1 | base image | BSD-2-Clause | reviewed: lib/ is BSD-2-Clause; GPL-2+ only programs/ and tests/ | liblz4.so.1 |
| liblzma5 | base image | public-domain | reviewed: liblzma; GPL-2+ only scripts and lib/ getopt | liblzma.so.5 |
| libmount1 | base image | LGPL-2.1-or-later | reviewed: util-linux: libmount is LGPL; GPL for the CLI tools | libmount.so.1 |
| libnghttp2-14 | base image | Expat | copyright `Files: *`; others: 4 | libnghttp2.so.14 |
| liborc-0.4-0t64 | base image | BSD-2-Clause | reviewed: Orc (copyright file not machine-readable) | liborc-0.4.so.0 |
| libpcre2-8-0 | base image | BSD-3-clause-Cambridge with BINARY LIBRARY-LIKE PACKAGES exception | copyright `Files: *`; others: 4 | libpcre2-8.so.0 |
| libpsl5t64 | base image | MIT | copyright `Files: *`; others: 2 | libpsl.so.5 |
| librdkafka1 | base image | BSD-2-clause | copyright `Files: *`; others: 7 | librdkafka.so.1 |
| libsasl2-2 | base image | BSD-3-Clause-Attribution | reviewed: Files: *; GPL only in debian/ packaging | libsasl2.so.2 |
| libselinux1 | base image | public-domain | reviewed: library; GPL-2 only utils/avcstat.c and debian/* | libselinux.so.1 |
| libsoup-3.0-0 | base image | LGPL-2+ | copyright `Files: *`; others: 5 | libsoup-3.0.so.0 |
| libsqlite3-0 | base image | blessing (public domain) | reviewed: GPL-2+ only debian/* | libsqlite3.so.0 |
| libssl3t64 | base image | Apache-2.0 | reviewed: OpenSSL 3; Artistic/GPL only a bundled Perl build module | libcrypto.so.3, libssl.so.3 |
| libstdc++6 | base image | GPL-3.0-or-later WITH GCC-exception-3.1 | reviewed: GCC runtime library exception | libstdc++.so.6 |
| libunistring5 | base image | LGPL-3.0-or-later OR GPL-2.0-or-later | reviewed: Files: * (dual) | libunistring.so.5 |
| libunwind8 | base image | MIT | reviewed: Expat | libunwind.so.8 |
| libyaml-cpp0.8 | base image | MIT | reviewed: X11; GPL-2.0+ only debian/* | libyaml-cpp.so.0.8 |
| libzstd1 | base image | BSD-3-Clause OR GPL-2.0-only | reviewed: dual licence, used under BSD-3-Clause | libzstd.so.1 |
| zlib1g | base image | Zlib | copyright `Files: *` | libz.so.1 |
| (NVIDIA DeepStream / CUDA, not in dpkg) | base image | NVIDIA proprietary (EULA) | path under /opt/nvidia or CUDA | libnvds_logger.so, libnvds_meta.so, libnvds_msgbroker.so, libnvdsgst_customhelper.so, libnvdsgst_helper.so, libnvdsgst_meta.so, libnvdsgst_smartrecord.so |

⚠ = needs a review by the project owner (copyleft beyond LGPL/GPL-with-exception, or non-commercial terms); see `docs/LICENSING.md` §6.
<!-- END GENERATED -->
