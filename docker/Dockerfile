# syntax=docker/dockerfile:1
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
#
# Two-stage RTVision image on public NVIDIA DeepStream.
#   build:   compiles the `rtvision` binary against the deepstream-app/apps-common sources and
#            headers that ship INSIDE the base image (never vendored, docs/LICENSING.md §3).
#   runtime: DeepStream runtime + the binary + librtvision_parsers.so + configs.
# Build (from the repo root):  docker build -f docker/Dockerfile -t rtvision:local .
# The resulting image contains NVIDIA software under NVIDIA's terms; do not publish it
# (docs/LICENSING.md §4).
#
# Entry point: entrypoint.sh -> rtvision-run (validates rtvision.yaml + model.yaml, generates
# the DeepStream configs, execs the binary). Runs as the non-root user `rtvision` (uid 1000).

ARG DS_IMAGE=nvcr.io/nvidia/deepstream:9.1-samples-multiarch

# ---------------------------------------------------------------------------------------------
FROM ${DS_IMAGE} AS build

# The samples image ships the CUDA runtime but not its headers: install the dev headers that
# exactly match the runtime already in the image (CUDA 13 splits crt/ into cuda-crt-X-Y).
RUN set -eux; \
    cuda_ver="$(ls -d /usr/local/cuda-*.* | sed 's|.*/cuda-||' | sort -V | tail -1 | tr . -)"; \
    cudart_ver="$(dpkg-query -W -f='${Version}' "cuda-cudart-${cuda_ver}")"; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        "cuda-cudart-dev-${cuda_ver}=${cudart_ver}" "cuda-crt-${cuda_ver}=${cudart_ver}" \
        libjson-glib-dev librdkafka-dev libsoup-3.0-dev; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY src/parsers/ src/parsers/
# Parser library + its unit and adapter tests (CPU only, no GPU needed at build time).
RUN make -C src/parsers clean && make -C src/parsers test && make -C src/parsers all
COPY src/app/ src/app/
COPY tests/golden/event_basic.json tests/golden/event_basic.json
RUN make -C src/app test && make -C src/app -j"$(nproc)" && make -C src/app info

# ---------------------------------------------------------------------------------------------
FROM ${DS_IMAGE} AS runtime

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends librdkafka1 libsoup-3.0-0 libjson-glib-1.0-0; \
    rm -rf /var/lib/apt/lists/*

# Python tools (rtvision-run). The image has no venv/ensurepip, so the package and its
# dependencies are installed into a private --target dir; system Python stays untouched. The onnx
# package's bundled backend test models are removed (unused; keeps model files out of the image).
COPY pyproject.toml README.md /tmp/rtvision-tools/
COPY tools/rtvision_tools/ /tmp/rtvision-tools/tools/rtvision_tools/
RUN set -eux; \
    python3 -m pip install --no-cache-dir --target /opt/rtvision/python /tmp/rtvision-tools; \
    rm -rf /tmp/rtvision-tools /opt/rtvision/python/onnx/backend/test/data; \
    mkdir -p /opt/rtvision/bin /opt/rtvision/engines /opt/rtvision/output; \
    printf '#!/bin/sh\nexec python3 -m rtvision_tools.run "$@"\n' > /opt/rtvision/bin/rtvision-run; \
    printf '#!/bin/sh\nexec python3 -m rtvision_tools.replicator.cli "$@"\n' > /opt/rtvision/bin/rtvision-replicator; \
    printf '#!/bin/sh\nexec python3 -m rtvision_tools.ui.server "$@"\n' > /opt/rtvision/bin/rtvision-ui; \
    chmod 0755 /opt/rtvision/bin/rtvision-run /opt/rtvision/bin/rtvision-replicator \
        /opt/rtvision/bin/rtvision-ui; \
    mkdir -p /opt/rtvision/videos /opt/rtvision/state

COPY --from=build /src/src/app/rtvision /opt/rtvision/bin/rtvision
COPY --from=build /src/src/parsers/librtvision_parsers.so /opt/rtvision/lib/librtvision_parsers.so
COPY schemas/ /opt/rtvision/schemas/
COPY models/examples/ /opt/rtvision/models/
COPY config/rtvision.example.yaml /opt/rtvision/config/rtvision.yaml

COPY docker/entrypoint.sh /opt/rtvision/bin/entrypoint.sh

# Non-root user (uid/gid 1000). The base image already has uid 1000 (`ubuntu`): rename it so
# files written to volumes have a predictable owner. Only the engine cache, the output dir and
# HOME (GStreamer registry cache) need to be writable.
RUN set -eux; \
    if getent passwd 1000 >/dev/null; then \
        old="$(getent passwd 1000 | cut -d: -f1)"; \
        usermod -l rtvision -d /home/rtvision -m "$old"; \
        groupmod -n rtvision "$(getent group 1000 | cut -d: -f1)"; \
    else \
        groupadd -g 1000 rtvision; useradd -u 1000 -g 1000 -m -d /home/rtvision rtvision; \
    fi; \
    chown -R 1000:1000 /opt/rtvision/engines /opt/rtvision/output /opt/rtvision/videos \
        /opt/rtvision/state; \
    chmod 0755 /opt/rtvision/bin/entrypoint.sh

ENV PATH=/opt/rtvision/bin:${PATH} \
    PYTHONPATH=/opt/rtvision/python \
    RTVISION_SCHEMA_DIR=/opt/rtvision/schemas \
    HOME=/home/rtvision
EXPOSE 9000 9464
STOPSIGNAL SIGTERM
USER 1000:1000
WORKDIR /home/rtvision

# /live answers 200 from the moment the binary starts (also while TensorRT builds the engine,
# see docs/DEVIATIONS.md #26); /startup and /ready are for orchestrators that support them.
HEALTHCHECK --interval=15s --timeout=3s --start-period=60s --retries=3 \
    CMD curl -fsS -o /dev/null "http://127.0.0.1:${RTVISION__OBSERVABILITY__OPS_PORT:-9464}/live" || exit 1

# Select the model with RTVISION_MODEL (name under /opt/rtvision/models or a path).
# The same image also runs the web UI (rtvision-ui) and the RTSP replicator (rtvision-replicator).
# `docker run IMAGE --dry-run` passes flags to rtvision-run; `docker run IMAGE bash` runs bash.
ENTRYPOINT ["entrypoint.sh"]
