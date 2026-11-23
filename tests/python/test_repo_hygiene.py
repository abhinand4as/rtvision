# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Repository hygiene: SPDX headers and forbidden files (docs/LICENSING.md)."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
CI = ROOT / "tools" / "ci"


def _run(script: str, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CI / script), *args], capture_output=True, text=True, check=False
    )


def test_spdx_headers_pass() -> None:
    res = _run("check_spdx.py")
    assert res.returncode == 0, res.stdout + res.stderr


def test_no_forbidden_files() -> None:
    res = _run("check_forbidden.py")
    assert res.returncode == 0, res.stdout + res.stderr


@pytest.mark.parametrize(
    ("name", "content", "ok"),
    [
        ("a.py", "# SPDX-License-Identifier: Apache-2.0\n# Copyright (c) 2026 X\n", True),
        ("a.py", "# SPDX-License-Identifier: Apache-2.0\n", False),  # code needs copyright
        ("a.c", "/* SPDX-License-Identifier: MIT */\n/* Copyright (c) 2026 X */\n", False),
        ("a.md", "<!-- SPDX-License-Identifier: Apache-2.0 -->\n", True),
        ("a.md", "# no header\n", False),
        ("a.xyz", "SPDX-License-Identifier: Apache-2.0\n", False),  # unknown type
        ("labels.txt", "person\n", True),  # exempt
        ("a.js", "// SPDX-License-Identifier: Apache-2.0\n// Copyright (c) 2026 X\n", True),
        ("a.css", "/* SPDX-License-Identifier: Apache-2.0 */\n", False),  # needs copyright
        (
            "d.c",
            "/*\n * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & "
            "AFFILIATES. All rights reserved.\n * SPDX-License-Identifier: Apache-2.0\n"
            " * Modifications Copyright (c) 2026 X\n * Changes: ported\n */\n",
            True,
        ),
        (
            "d.c",
            "/*\n * SPDX-FileCopyrightText: Copyright (c) 2025-2026, NVIDIA CORPORATION & "
            "AFFILIATES. All rights\n * reserved. SPDX-License-Identifier: Apache-2.0\n"
            " * Modifications Copyright (c) 2026 X\n * Changes: ported\n */\n",
            False,  # re-wrapped NVIDIA notice
        ),
    ],
)
def test_spdx_rules(tmp_path: Path, name: str, content: str, ok: bool) -> None:
    (tmp_path / name).write_text(content)
    res = _run("check_spdx.py", "--root", str(tmp_path))
    assert (res.returncode == 0) is ok, res.stdout


@pytest.mark.parametrize(
    ("name", "ok"),
    [
        ("model.onnx", False),
        ("weights/best.pt", False),
        ("clip.mp4", False),
        ("vendor/deepstream_app.c", False),
        ("includes/nvdsinfer_custom_impl.h", False),
        ("src/app/main.c", True),
    ],
)
def test_forbidden_rules(tmp_path: Path, name: str, ok: bool) -> None:
    target = tmp_path / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(b"x")
    res = _run("check_forbidden.py", "--root", str(tmp_path))
    assert (res.returncode == 0) is ok, res.stdout


def test_dockerignore_excludes_weights_in_subdirectories() -> None:
    """.dockerignore patterns are rooted at the build context (unlike .gitignore): `*.onnx` alone
    would let models/examples/*/x.onnx into the image."""
    lines = {line.strip() for line in (ROOT / ".dockerignore").read_text().splitlines()}
    for ext in ("pt", "pth", "onnx", "engine", "plan", "etlt", "safetensors", "mp4"):
        assert f"**/*.{ext}" in lines, f".dockerignore must contain **/*.{ext}"
