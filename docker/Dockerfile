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
# Entry point: perceptra-run (validates perceptra.yaml + model.yaml, generates the DeepStream
# configs, execs the binary). Non-root user, HEALTHCHECK and entrypoint.sh follow in M7.

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
RUN make -C src/app -j"$(nproc)" && make -C src/app info

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

ENV PATH=/opt/perceptra/bin:${PATH} \
    PYTHONPATH=/opt/perceptra/python \
    PERCEPTRA_SCHEMA_DIR=/opt/perceptra/schemas
EXPOSE 9000 9464
STOPSIGNAL SIGTERM

# Select the model with PERCEPTRA_MODEL (name under /opt/perceptra/models or a path).
ENTRYPOINT ["perceptra-run"]
