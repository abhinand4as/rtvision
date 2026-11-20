# syntax=docker/dockerfile:1
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
#
# Two-stage Perceptra image on public NVIDIA DeepStream.
#   build:   compiles the `perceptra` binary against the deepstream-app/apps-common sources and
#            headers that ship INSIDE the base image (never vendored, docs/LICENSING.md §3).
#   runtime: DeepStream runtime + the binary + libperceptra_parsers.so + configs.
# Build (from the repo root):  docker build -f docker/Dockerfile -t perceptra:local .
# The resulting image contains NVIDIA software under NVIDIA's terms; do not publish it
# (docs/LICENSING.md §4).
#
# Entry point: entrypoint.sh -> perceptra-run (validates perceptra.yaml + model.yaml, generates
# the DeepStream configs, execs the binary). Runs as the non-root user `perceptra` (uid 1000).

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

# Python tools (perceptra-run). The image has no venv/ensurepip, so the package and its
# dependencies are installed into a private --target dir; system Python stays untouched.
COPY pyproject.toml README.md /tmp/perceptra-tools/
COPY tools/perceptra_tools/ /tmp/perceptra-tools/tools/perceptra_tools/
RUN set -eux; \
    python3 -m pip install --no-cache-dir --target /opt/perceptra/python /tmp/perceptra-tools; \
    rm -rf /tmp/perceptra-tools; \
    mkdir -p /opt/perceptra/bin /opt/perceptra/engines /opt/perceptra/output; \
    printf '#!/bin/sh\nexec python3 -m perceptra_tools.run "$@"\n' > /opt/perceptra/bin/perceptra-run; \
    chmod 0755 /opt/perceptra/bin/perceptra-run

COPY --from=build /src/src/app/perceptra /opt/perceptra/bin/perceptra
COPY --from=build /src/src/parsers/libperceptra_parsers.so /opt/perceptra/lib/libperceptra_parsers.so
COPY schemas/ /opt/perceptra/schemas/
COPY models/examples/ /opt/perceptra/models/
COPY config/perceptra.example.yaml /opt/perceptra/config/perceptra.yaml

COPY docker/entrypoint.sh /opt/perceptra/bin/entrypoint.sh

# Non-root user (uid/gid 1000). The base image already has uid 1000 (`ubuntu`): rename it so
# files written to volumes have a predictable owner. Only the engine cache, the output dir and
# HOME (GStreamer registry cache) need to be writable.
RUN set -eux; \
    if getent passwd 1000 >/dev/null; then \
        old="$(getent passwd 1000 | cut -d: -f1)"; \
        usermod -l perceptra -d /home/perceptra -m "$old"; \
        groupmod -n perceptra "$(getent group 1000 | cut -d: -f1)"; \
    else \
        groupadd -g 1000 perceptra; useradd -u 1000 -g 1000 -m -d /home/perceptra perceptra; \
    fi; \
    chown -R 1000:1000 /opt/perceptra/engines /opt/perceptra/output; \
    chmod 0755 /opt/perceptra/bin/entrypoint.sh

ENV PATH=/opt/perceptra/bin:${PATH} \
    PYTHONPATH=/opt/perceptra/python \
    PERCEPTRA_SCHEMA_DIR=/opt/perceptra/schemas \
    HOME=/home/perceptra
EXPOSE 9000 9464
STOPSIGNAL SIGTERM
USER 1000:1000
WORKDIR /home/perceptra

# /live answers 200 from the moment the binary starts (also while TensorRT builds the engine,
# see docs/DEVIATIONS.md #26); /startup and /ready are for orchestrators that support them.
HEALTHCHECK --interval=15s --timeout=3s --start-period=60s --retries=3 \
    CMD curl -fsS -o /dev/null "http://127.0.0.1:${PERCEPTRA__OBSERVABILITY__OPS_PORT:-9464}/live" || exit 1

# Select the model with PERCEPTRA_MODEL (name under /opt/perceptra/models or a path).
# `docker run IMAGE --dry-run` passes flags to perceptra-run; `docker run IMAGE bash` runs bash.
ENTRYPOINT ["entrypoint.sh"]
