#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Fail if the repository contains files that must never be committed.

See docs/LICENSING.md §3: model weights/engines, media, DeepStream SDK sources/headers,
and oversized blobs. Checks tracked files plus untracked-but-not-ignored files.
"""

from __future__ import annotations

import argparse
import fnmatch
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_spdx import list_files, repo_root  # noqa: E402

# Model weights / engines / calibration blobs.
WEIGHT_EXTS = {".pt", ".pth", ".onnx", ".engine", ".plan", ".etlt", ".trt", ".safetensors"}
# Media.
MEDIA_EXTS = {".mp4", ".mkv", ".avi", ".mov", ".h264", ".h265", ".264", ".265"}
# DeepStream SDK sources/headers that must be compiled from inside the base image, never vendored.
DEEPSTREAM_PATTERNS = [
    "**/deepstream_app.c",
    "**/deepstream_app.h",
    "**/deepstream_app_config_parser*.c",
    "**/deepstream_config_file_parser.*",
    "**/apps-common/**",
    "**/nvdsinfer*.h",
    "**/nvdsmeta*.h",
    "**/nvds_*.h",
    "**/gstnvdsmeta.h",
    "**/libnvds*.so*",
]
MAX_BYTES = 5 * 1024 * 1024


def violations(root: Path) -> list[tuple[str, str]]:
    found: list[tuple[str, str]] = []
    for path in list_files(root):
        suffix = Path(path).suffix.lower()
        if suffix in WEIGHT_EXTS:
            found.append((path, "model weights/engine"))
        elif suffix in MEDIA_EXTS:
            found.append((path, "video/media file"))
        elif any(
            fnmatch.fnmatch("/" + path, "*/" + p.removeprefix("**/")) for p in DEEPSTREAM_PATTERNS
        ):
            found.append((path, "DeepStream SDK file (reference it inside the container instead)"))
        elif (root / path).stat().st_size > MAX_BYTES:
            found.append((path, f"file larger than {MAX_BYTES // (1024 * 1024)} MiB"))
    return found


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", type=Path, default=repo_root())
    args = ap.parse_args(argv)
    found = violations(args.root.resolve())
    for path, why in found:
        print(f"FAIL  {path}: {why}")
    print(f"{'FAIL' if found else 'PASS'}: forbidden-file check ({len(found)} violations)")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
