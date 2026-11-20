<!-- SPDX-License-Identifier: Apache-2.0 -->
# Deviations from the specification

The implementing agent records here every place where the implementation differs from ARCHITECTURE.md or the docs, for example because the DeepStream 9.1 public headers or behaviour differ from what the spec assumed.

| # | Date | Spec reference | Deviation | Reason / evidence (header path, doc link, test) |
|---|---|---|---|---|
| 1 | 2026-11-20 | docs/LICENSING.md §7 ("SPDX headers: every source/config/script file") | JSON files cannot hold comments: `schemas/*.json` carry `"$comment": "SPDX-License-Identifier: Apache-2.0"`; golden test data under `tests/golden/` and `labels.txt` files are exempt from the header check. Docs/config files need the SPDX line only; code files also need a copyright line. | JSON spec (RFC 8259) has no comment syntax; `$comment` is a JSON Schema 2020-12 keyword. Rules in `tools/ci/check_spdx.py`. |
