# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Host hardware readings: CPU, RAM, disk (from /proc) and NVIDIA GPUs (from nvidia-smi)."""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path
from typing import Any

GPU_FIELDS = (
    ("index", "index", int),
    ("name", "name", str),
    ("driver", "driver_version", str),
    ("utilization", "utilization.gpu", float),
    ("encoder", "utilization.encoder", float),
    ("decoder", "utilization.decoder", float),
    ("memory_used_mib", "memory.used", float),
    ("memory_total_mib", "memory.total", float),
    ("temperature_c", "temperature.gpu", float),
    ("power_w", "power.draw", float),
    ("power_limit_w", "power.limit", float),
)


def parse_gpus(text: str) -> list[dict[str, Any]]:
    """nvidia-smi CSV -> dicts; values the GPU does not report ([N/A]) become None."""
    gpus = []
    for line in text.strip().splitlines():
        cells = [c.strip() for c in line.split(",")]
        if len(cells) != len(GPU_FIELDS):
            continue
        gpu: dict[str, Any] = {}
        for (key, _field, kind), cell in zip(GPU_FIELDS, cells, strict=True):
            try:
                gpu[key] = kind(cell) if not cell.startswith("[") else None
            except ValueError:
                gpu[key] = None
        gpus.append(gpu)
    return gpus


def read_gpus() -> tuple[list[dict[str, Any]], str | None]:
    query = ",".join(f for _k, f, _t in GPU_FIELDS)
    try:
        out = subprocess.run(
            ["nvidia-smi", f"--query-gpu={query}", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return [], f"nvidia-smi: {exc}"
    if out.returncode != 0:
        return [], f"nvidia-smi: {(out.stdout or out.stderr).strip()[:200]}"
    return parse_gpus(out.stdout), None


def parse_cpu_times(stat: str) -> list[tuple[int, int]]:
    """/proc/stat -> [(busy, total)], first the whole CPU, then each core."""
    out = []
    for line in stat.splitlines():
        if not line.startswith("cpu"):
            break
        v = [int(x) for x in line.split()[1:9]]
        idle = v[3] + v[4]  # idle + iowait
        out.append((sum(v) - idle, sum(v)))
    return out


def cpu_percent(before: list[tuple[int, int]], after: list[tuple[int, int]]) -> list[float]:
    result = []
    for (b0, t0), (b1, t1) in zip(before, after, strict=False):
        total = t1 - t0
        result.append(round(100.0 * (b1 - b0) / total, 1) if total > 0 else 0.0)
    return result


def parse_meminfo(text: str) -> dict[str, int]:
    """Memory in bytes: total, available, used, swap_total, swap_used."""
    kb: dict[str, int] = {}
    for line in text.splitlines():
        key, _, rest = line.partition(":")
        parts = rest.split()
        if parts and parts[0].isdigit():
            kb[key] = int(parts[0])
    total, avail = kb.get("MemTotal", 0), kb.get("MemAvailable", 0)
    swap = kb.get("SwapTotal", 0)
    return {
        "total": total * 1024,
        "available": avail * 1024,
        "used": (total - avail) * 1024,
        "swap_total": swap * 1024,
        "swap_used": (swap - kb.get("SwapFree", 0)) * 1024,
    }


def cpu_model(cpuinfo: str) -> str | None:
    for line in cpuinfo.splitlines():
        if line.startswith("model name"):
            return line.partition(":")[2].strip()
    return None


class HardwareMonitor:
    """`sample()` is called periodically by one thread; CPU usage covers the time in between."""

    def __init__(self, proc: Path = Path("/proc")) -> None:
        self._proc = proc
        self._model = cpu_model(self._read("cpuinfo"))
        self._last = parse_cpu_times(self._read("stat"))

    def _read(self, name: str) -> str:
        try:
            return (self._proc / name).read_text(encoding="utf-8", errors="replace")
        except OSError:
            return ""

    def sample(self) -> dict[str, Any]:
        times = parse_cpu_times(self._read("stat"))
        percent = cpu_percent(self._last, times)
        self._last = times
        gpus, gpu_error = read_gpus()
        uptime = self._read("uptime").split()
        load = self._read("loadavg").split()[:3]
        disk = shutil.disk_usage("/")
        return {
            "cpu": {
                "model": self._model,
                "cores": max(len(percent) - 1, 0) or os.cpu_count(),
                "percent": percent[0] if percent else None,
                "per_core": percent[1:],
                "load": [float(x) for x in load],
            },
            "memory": parse_meminfo(self._read("meminfo")),
            "disk": {"total": disk.total, "used": disk.used, "free": disk.free},
            "gpus": gpus,
            "gpu_error": gpu_error,
            "uptime_s": float(uptime[0]) if uptime else None,
        }
