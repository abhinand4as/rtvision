# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Replicator configuration: YAML file or command-line options -> validated streams."""

from __future__ import annotations

import os
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

import yaml

from ..schemas import ConfigError, apply_defaults, load_schema, load_yaml, validate

SCHEMA = "replicator_config.schema.json"


@dataclass(frozen=True)
class StreamSpec:
    name: str
    video: str
    loop: bool = True
    start_offset_s: float = 0.0
    enabled: bool = True

    @property
    def path(self) -> str:
        return f"/{self.name}"


@dataclass(frozen=True)
class ReplicatorConfig:
    rtsp_port: int = 8554
    public_host: str | None = None
    streams: tuple[StreamSpec, ...] = field(default_factory=tuple)

    def to_dict(self) -> dict[str, Any]:
        return {
            "rtsp_port": self.rtsp_port,
            "public_host": self.public_host,
            "streams": [asdict(s) for s in self.streams],
        }


def parse_config(data: Any, source: str = "config") -> ReplicatorConfig:
    """Validates (schema + unique names) and fills defaults. Raises ConfigError."""
    if data is None:
        data = {}
    schema = load_schema(SCHEMA)
    validate(data, schema, source)
    data = apply_defaults(data, schema)
    item_schema = schema["properties"]["streams"]["items"]
    streams = [StreamSpec(**apply_defaults(s, item_schema)) for s in data["streams"]]
    seen: set[str] = set()
    for i, s in enumerate(streams):
        if s.name in seen:
            raise ConfigError(f"{source}: streams[{i}].name: duplicate stream name '{s.name}'")
        seen.add(s.name)
    return ReplicatorConfig(data["rtsp_port"], data["public_host"], tuple(streams))


def load_config(path: Path) -> ReplicatorConfig:
    return parse_config(load_yaml(path), str(path))


def save_config(cfg: ReplicatorConfig, path: Path) -> None:
    """Atomically replaces the YAML file, so a crash never leaves a half-written config."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f".{path.name}.tmp")
    header = "# RTVision RTSP replicator configuration (written by rtvision-replicator).\n"
    tmp.write_text(header + yaml.safe_dump(cfg.to_dict(), sort_keys=False), encoding="utf-8")
    os.replace(tmp, path)


def config_from_videos(
    videos: list[str],
    count: int | None = None,
    prefix: str = "cam",
    rtsp_port: int = 8554,
    loop: bool = True,
    stagger_s: float = 0.0,
) -> ReplicatorConfig:
    """`count` streams (default: one per video) using the videos round robin. Streams that reuse
    a video start `stagger_s` seconds apart, so they do not look identical."""
    if not videos:
        raise ConfigError("at least one video is required")
    count = len(videos) if count is None else count
    if count < 1:
        raise ConfigError("the number of streams must be at least 1")
    streams = [
        {
            "name": f"{prefix}-{i + 1}",
            "video": videos[i % len(videos)],
            "loop": loop,
            "start_offset_s": (i // len(videos)) * stagger_s,
        }
        for i in range(count)
    ]
    return parse_config({"rtsp_port": rtsp_port, "streams": streams}, "command line")
