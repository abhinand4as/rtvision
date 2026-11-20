<!-- SPDX-License-Identifier: Apache-2.0 -->
# Security policy

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Report them privately to
Abhinand A S (use GitHub's "Report a vulnerability" / private security advisory on this
repository). Include a description, affected version or commit, and reproduction steps.
We aim to acknowledge reports within 5 working days.

## Supported versions

Only the latest release receives security fixes until v1.0.

## Deployment notes

- The stream REST API (port 9000, DeepStream `nvmultiurisrcbin`) and the ops API (port 9464)
  are **unauthenticated**. Bind them to trusted networks or put an authenticating gateway in
  front. See ARCHITECTURE.md §11.
- Kafka credentials are passed via `publish.kafka.properties` / environment; never commit them.
- Container images include NVIDIA components; track NVIDIA security bulletins for DeepStream,
  TensorRT and CUDA.
