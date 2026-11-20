#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Check that every tracked file carries the required SPDX licence header.

Rules (see CLAUDE.md and docs/LICENSING.md):
  * Code files (C/C++, Python, shell, Jinja templates, Makefile, Dockerfile) need
    ``SPDX-License-Identifier: Apache-2.0`` and a ``Copyright (c) <YEAR> ...`` line.
    Files derived from RT-CV satisfy this with NVIDIA's original lines plus the
    ``Modifications Copyright`` line.
  * Docs and config files (Markdown, YAML, INI, TOML, JSON Schema, dotfiles) need the
    SPDX identifier only. JSON has no comments, so schemas carry it in ``"$comment"``.
  * Files matching ``EXEMPT`` are skipped (licence texts, data files, golden outputs).
  * Any other file type fails, so new kinds of files get an explicit decision.

Headers are searched for in the first ``HEADER_LINES`` lines. Exit code 0 = pass.
"""

from __future__ import annotations

import argparse
import fnmatch
import itertools
import os
import re
import subprocess
import sys
from pathlib import Path

HEADER_LINES = 20
SPDX_RE = re.compile(r"SPDX-License-Identifier:\s*Apache-2\.0\b")
COPYRIGHT_RE = re.compile(r"Copyright \(c\) \d{4}")

CODE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp", ".py", ".sh", ".j2"}
CODE_NAMES = {"Makefile", "Dockerfile"}
DOC_SUFFIXES = {".md", ".yaml", ".yml", ".ini", ".toml", ".json", ".cfg"}
DOC_NAMES = {".gitignore", ".dockerignore", ".editorconfig", ".clang-format", ".env.example"}

EXEMPT = [
    "LICENSE",
    "NOTICE",
    "**/.gitkeep",
    "**/labels.txt",
    "tests/golden/**",
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def list_files(root: Path) -> list[str]:
    """Tracked + untracked-but-not-ignored files (works before the first commit too)."""
    try:
        out = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
            cwd=root,
            check=True,
            capture_output=True,
        ).stdout.decode()
        files = [f for f in out.split("\0") if f]
    except (OSError, subprocess.CalledProcessError):
        files = []
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in {".git", ".venv", "__pycache__"}]
            for name in filenames:
                files.append(str(Path(dirpath, name).relative_to(root)))
    return sorted(f for f in files if (root / f).is_file())


def is_exempt(path: str) -> bool:
    return any(fnmatch.fnmatch(path, pat) or path == pat for pat in EXEMPT) or any(
        pat.startswith("**/") and fnmatch.fnmatch(Path(path).name, pat[3:]) for pat in EXEMPT
    )


def classify(path: str) -> str | None:
    p = Path(path)
    if p.name in CODE_NAMES or p.suffix in CODE_SUFFIXES:
        return "code"
    if p.name in DOC_NAMES or p.suffix in DOC_SUFFIXES:
        return "doc"
    return None


def check_file(root: Path, path: str) -> str | None:
    """Return an error message, or None if the file passes."""
    kind = classify(path)
    if kind is None:
        return "unknown file type: add an SPDX rule or an EXEMPT pattern in tools/ci/check_spdx.py"
    try:
        with open(root / path, encoding="utf-8") as fh:
            head = "".join(itertools.islice(fh, HEADER_LINES))
    except UnicodeDecodeError:
        return "binary or non-UTF-8 file"
    if not SPDX_RE.search(head):
        return "missing 'SPDX-License-Identifier: Apache-2.0'"
    if kind == "code" and not COPYRIGHT_RE.search(head):
        return "missing 'Copyright (c) <YEAR> <OWNER>' line"
    return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=Path, default=repo_root())
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    root: Path = args.root.resolve()
    errors: list[tuple[str, str]] = []
    checked = 0
    for path in list_files(root):
        if is_exempt(path):
            continue
        checked += 1
        err = check_file(root, path)
        if err:
            errors.append((path, err))
        elif args.verbose:
            print(f"ok    {path}")

    for path, err in errors:
        print(f"FAIL  {path}: {err}")
    status = "FAIL" if errors else "PASS"
    print(f"{status}: SPDX header check ({checked} files checked, {len(errors)} failing)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
