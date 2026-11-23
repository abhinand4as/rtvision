# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""The replicator's videos directory: safe names, listing, streaming uploads, deletion."""

from __future__ import annotations

import os
import re
import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any, BinaryIO

EXTENSIONS = (".mp4", ".mov", ".m4v", ".mkv", ".ts")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._ ()+-]{0,127}$")
DEFAULT_MAX_UPLOAD = 4 * 1024**3
MIN_FREE_BYTES = 512 * 1024**2
CHUNK = 1024 * 1024


class LibraryError(Exception):
    """Request the user can fix. `status` is the matching HTTP status code."""

    def __init__(self, message: str, status: int = 400) -> None:
        super().__init__(message)
        self.status = status


def safe_name(name: str) -> str:
    """Validates a plain file name with a supported extension (no directories)."""
    if not NAME_RE.match(name) or ".." in name:
        raise LibraryError(
            f"invalid file name '{name}': use letters, digits, spaces and . _ - ( ) + only"
        )
    if not name.lower().endswith(EXTENSIONS):
        raise LibraryError(f"unsupported file type '{name}' (supported: {', '.join(EXTENSIONS)})")
    return name


class VideoLibrary:
    def __init__(
        self,
        directory: Path,
        probe: Callable[[Path], dict[str, Any]] | None = None,
        max_upload: int = DEFAULT_MAX_UPLOAD,
    ) -> None:
        self.directory = directory
        self.max_upload = max_upload
        self._probe = probe
        self._cache: dict[tuple[str, int, int], dict[str, Any]] = {}

    def path(self, name: str) -> Path:
        return self.directory / safe_name(name)

    def info(self, name: str) -> dict[str, Any]:
        """Size plus probe results (codec, resolution, fps, duration) or an `error`."""
        path = self.path(name)
        try:
            st = path.stat()
        except FileNotFoundError as exc:
            raise LibraryError(f"video '{name}' not found", 404) from exc
        key = (name, st.st_size, st.st_mtime_ns)
        if key not in self._cache:
            details: dict[str, Any] = {}
            if self._probe:
                try:
                    details = self._probe(path)
                except Exception as exc:  # noqa: BLE001 - any probe failure is reported, not fatal
                    details = {"error": str(exc)}
            self._cache[key] = details
        return {"name": name, "size_bytes": st.st_size, **self._cache[key]}

    def names(self) -> list[str]:
        if not self.directory.is_dir():
            return []
        return sorted(
            p.name
            for p in self.directory.iterdir()
            if p.is_file() and NAME_RE.match(p.name) and p.name.lower().endswith(EXTENSIONS)
        )

    def list(self) -> list[dict[str, Any]]:
        return [self.info(n) for n in self.names()]

    def store(self, name: str, stream: BinaryIO, length: int) -> dict[str, Any]:
        """Streams an upload to disk; the file appears only when complete and playable."""
        target = self.path(name)
        if length <= 0:
            raise LibraryError("empty upload (Content-Length is required)", 411)
        if length > self.max_upload:
            raise LibraryError(f"file too large (limit {self.max_upload // 1024**2} MiB)", 413)
        self.directory.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(self.directory).free - length < MIN_FREE_BYTES:
            raise LibraryError("not enough free disk space for this upload", 507)
        # The probe needs the real extension, so the temporary name keeps it (and is hidden).
        tmp = target.with_name(f".upload-{os.getpid()}-{id(stream)}-{name}")
        try:
            left = length
            with open(tmp, "wb") as fh:
                while left > 0:
                    chunk = stream.read(min(CHUNK, left))
                    if not chunk:
                        raise LibraryError("upload ended early", 400)
                    fh.write(chunk)
                    left -= len(chunk)
            if self._probe:
                try:
                    details = self._probe(tmp)
                except Exception as exc:  # noqa: BLE001
                    raise LibraryError(f"not a usable video: {exc}", 415) from exc
                if details.get("error"):
                    raise LibraryError(details["error"], 415)
            os.replace(tmp, target)
        finally:
            tmp.unlink(missing_ok=True)
        return self.info(name)

    def delete(self, name: str) -> None:
        try:
            self.path(name).unlink()
        except FileNotFoundError as exc:
            raise LibraryError(f"video '{name}' not found", 404) from exc
