<!-- SPDX-License-Identifier: Apache-2.0 -->
# Contributing to RTVision

Thank you for contributing! Please read [ARCHITECTURE.md](ARCHITECTURE.md) and
[docs/IMPLEMENTATION_PLAN.md](docs/IMPLEMENTATION_PLAN.md) first.

## Licence of contributions

RTVision is licensed under the [Apache License 2.0](LICENSE). By submitting a contribution
you agree that it is licensed under Apache-2.0 (Apache-2.0 §5).

## Developer Certificate of Origin (DCO)

Every commit must be signed off, certifying the
[Developer Certificate of Origin 1.1](https://developercertificate.org/):

```bash
git commit -s -m "feat: short description"
```

This adds a `Signed-off-by: Your Name <you@example.com>` trailer. Commits without it cannot be
merged.

## Rules (see docs/LICENSING.md)

- **Never commit** model weights (`*.pt`, `*.onnx`, `*.engine`, `*.plan`, `*.etlt`, …), videos,
  DeepStream binaries/headers/sample sources, or anything downloaded from NGC.
- **Never copy code** from GPL/AGPL projects (for example Ultralytics). Do not add dependencies
  that are not on the allow-list in `docs/LICENSING.md` §6 without the project owner's approval.
- **SPDX header** on every new source, config and script file:
  `SPDX-License-Identifier: Apache-2.0` and `Copyright (c) <YEAR> Abhinand A S`.
  Files derived from NVIDIA RT-CV keep NVIDIA's original lines, add
  `Modifications Copyright (c) <YEAR> Abhinand A S` plus a one-line change summary, and are
  listed in `NOTICE`.

## Commits and pull requests

- Small, reviewable commits, one logical change each.
- [Conventional Commits](https://www.conventionalcommits.org/): `feat:`, `fix:`, `docs:`,
  `test:`, `build:`, `chore:`.
- Update `CHANGELOG.md` for user-visible changes.

## Local checks (same as CI)

```bash
python3 tools/ci/check_spdx.py
python3 tools/ci/check_forbidden.py
ruff check . && ruff format --check .
git ls-files '*.c' '*.h' '*.cpp' '*.hpp' | xargs -r clang-format --dry-run --Werror
pytest -q
shellcheck -x $(git ls-files '*.sh')
```
