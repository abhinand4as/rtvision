# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Talks to the RTVision ops server and stream REST API; keeps a short metrics history."""

from __future__ import annotations

import json
import os
import re
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from collections.abc import Callable
from pathlib import Path
from typing import Any
from urllib.parse import urlsplit

from ..gen_configs import tiler
from .hardware import HardwareMonitor

SAMPLE_S = 2.0
HISTORY = 150  # samples: 5 minutes
REMOVE_CHECKS = 24  # x 0.25 s
EVENT_MAX_AGE_MS = 3000  # older events mean "nothing detected right now"
URL_SCHEMES = ("rtsp", "rtsps", "file", "http", "https")
CAMERA_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,62}$")
_SAMPLE_RE = re.compile(r"^([a-zA-Z_:][\w:]*)(?:\{(.*)\})?\s+(\S+)$")
_LABEL_RE = re.compile(r'([a-zA-Z_]\w*)="((?:[^"\\]|\\.)*)"')
_ESCAPES = {"\\n": "\n", '\\"': '"', "\\\\": "\\"}


class BackendError(Exception):
    def __init__(self, message: str, status: int = 502) -> None:
        super().__init__(message)
        self.status = status


def parse_prometheus(text: str) -> list[tuple[str, dict[str, str], float]]:
    out = []
    for line in text.splitlines():
        m = _SAMPLE_RE.match(line.strip())
        if not m:
            continue
        labels = {
            k: re.sub(r'\\[n"\\]', lambda e: _ESCAPES[e.group(0)], v)
            for k, v in _LABEL_RE.findall(m.group(2) or "")
        }
        try:
            out.append((m.group(1), labels, float(m.group(3))))
        except ValueError:
            continue
    return out


def http(
    url: str, method: str = "GET", body: bytes | None = None, timeout: float = 4
) -> tuple[int, bytes]:
    req = urllib.request.Request(url, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed http URLs
            return resp.status, resp.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()
    except (OSError, ValueError) as exc:
        raise BackendError(f"{urlsplit(url).netloc} is not reachable") from exc


class CameraUrls:
    """URLs of the cameras added through the UI. DeepStream needs the exact URL to remove a stream
    but does not report it. URLs can contain passwords: the file is private (0600) and its content
    is never sent to the browser."""

    def __init__(self, path: Path | None = None) -> None:
        self._path = path
        self._lock = threading.Lock()
        self._urls: dict[str, str] = {}
        if path and path.is_file():
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                self._urls = {str(k): str(v) for k, v in data.items()}
            except (OSError, ValueError, AttributeError):
                self._urls = {}

    def get(self, camera_id: str) -> str | None:
        with self._lock:
            return self._urls.get(camera_id)

    def set(self, camera_id: str, url: str | None) -> None:
        with self._lock:
            if url is None:
                self._urls.pop(camera_id, None)
            else:
                self._urls[camera_id] = url
            if self._path:
                tmp = self._path.with_name(f".{self._path.name}.tmp")
                fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                with os.fdopen(fd, "w", encoding="utf-8") as fh:
                    json.dump(self._urls, fh)
                os.replace(tmp, self._path)


class RTVisionClient:
    def __init__(
        self,
        ops_url: str,
        rest_url: str,
        fetch: Callable[..., Any] = http,
        urls: CameraUrls | None = None,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        self.urls = urls or CameraUrls()
        self._sleep = sleep
        self.ops = ops_url.rstrip("/")
        self.rest = rest_url.rstrip("/")
        self._fetch = fetch

    def _json(self, url: str) -> Any:
        status, body = self._fetch(url)
        try:
            return json.loads(body)
        except ValueError as exc:
            raise BackendError(f"unexpected answer from {url} (HTTP {status})") from exc

    def streams(self) -> list[dict[str, Any]]:
        data = self._json(f"{self.rest}/api/v1/stream/get-stream-info")
        info = (data.get("stream-info") or {}).get("stream-info") or []
        found = [
            {
                "id": str(s.get("camera_id", "")),
                "name": str(s.get("camera_name") or s.get("camera_id", "")),
                "index": int(s.get("source_id", -1)),
            }
            for s in info
        ]
        return sorted(found, key=lambda s: s["index"])

    def overview(self) -> dict[str, Any]:
        """Everything the Live and Performance pages need, in one call."""
        info = self._json(f"{self.ops}/info")
        ready_status, ready_body = self._fetch(f"{self.ops}/ready")
        _, metrics_body = self._fetch(f"{self.ops}/metrics")
        latest = self._json(f"{self.ops}/events/latest").get("events", [])
        try:
            streams = self.streams()
        except BackendError:
            streams = []  # the REST server only starts with the pipeline
        try:
            ready_reason = json.loads(ready_body).get("status")
        except ValueError:
            ready_reason = None

        fps: dict[str, float] = {}
        frames: dict[str, float] = {}
        objects: dict[str, dict[str, float]] = {}
        totals: dict[str, float] = {}
        for name, labels, value in parse_prometheus(metrics_body.decode(errors="replace")):
            sensor = labels.get("sensor_id", "")
            if name == "rtvision_stream_fps":
                fps[sensor] = value
            elif name == "rtvision_frames_processed_total":
                frames[sensor] = value
            elif name == "rtvision_objects_detected_total":
                objects.setdefault(sensor, {})[labels.get("label", "")] = value
            elif not labels or name == "rtvision_model_swaps_total":
                key = name + (f"_{labels['result']}" if "result" in labels else "")
                totals[key] = totals.get(key, 0) + value

        events = {e["event"]["sensor"]["id"]: e for e in latest}
        for s in streams:
            e = events.get(s["id"])
            fresh = e is not None and e["age_ms"] <= EVENT_MAX_AGE_MS
            counts: dict[str, int] = {}
            for o in e["event"]["objects"] if fresh else []:
                counts[o["label"]] = counts.get(o["label"], 0) + 1
            s["fps"] = fps.get(s["id"], 0.0)
            s["frames"] = int(frames.get(s["id"], 0))
            s["detections_total"] = int(sum(objects.get(s["id"], {}).values()))
            s["objects"] = dict(sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])))
            s["frame"] = e["event"]["frame"] if fresh else None
            s["timestamp_source"] = e["event"].get("timestamp_source") if e else None
            s["event_age_ms"] = e["age_ms"] if e else None
        rows, cols = tiler(int(info.get("max_streams") or 1))
        return {
            "ready": ready_status == 200,
            "ready_reason": ready_reason,
            "info": info,
            "tiler": {"rows": rows, "cols": cols},
            "streams": streams,
            "totals": totals,
        }

    def _envelope(self, change: str, camera_id: str, name: str, url: str) -> bytes:
        return json.dumps(
            {
                "key": "sensor",
                "value": {
                    "camera_id": camera_id,
                    "camera_name": name,
                    "camera_url": url,
                    "change": change,
                },
                "headers": {"source": "rtvision-ui"},
            }
        ).encode()

    def _stream_call(self, what: str, body: bytes) -> None:
        status, answer = self._fetch(f"{self.rest}/api/v1/stream/{what}", "POST", body, timeout=15)
        if status != 200:
            try:
                reason = json.loads(answer).get("reason", "")
            except ValueError:
                reason = ""
            raise BackendError(reason or f"stream/{what} failed (HTTP {status})", 409)

    def add_stream(self, data: Any) -> dict[str, str]:
        if not isinstance(data, dict):
            raise BackendError("a JSON object is required", 400)
        camera_id = str(data.get("id", "")).strip()
        url = str(data.get("url", "")).strip()
        name = str(data.get("name") or camera_id).strip()[:128]
        if not CAMERA_ID_RE.match(camera_id):
            raise BackendError("camera id: use letters, digits, '.', '_' and '-' only", 400)
        if urlsplit(url).scheme not in URL_SCHEMES or any(c.isspace() for c in url):
            raise BackendError(f"camera URL must start with {', '.join(URL_SCHEMES)}://", 400)
        # Check first, for a clearer message than DeepStream's.
        current = self.streams()
        if any(s["id"] == camera_id for s in current):
            raise BackendError(f"camera '{camera_id}' is already added", 409)
        limit = int(self._json(f"{self.ops}/info").get("max_streams") or 0)
        if limit and len(current) >= limit:
            raise BackendError(f"all {limit} stream slots are in use", 409)
        self._stream_call("add", self._envelope("camera_add", camera_id, name, url))
        self.urls.set(camera_id, url)
        return {"added": camera_id}

    def remove_stream(self, camera_id: str, url: str | None = None) -> dict[str, str]:
        if not CAMERA_ID_RE.match(camera_id):
            raise BackendError("invalid camera id", 400)
        url = url or self.urls.get(camera_id)
        if not url:
            raise BackendError(
                f"camera '{camera_id}' was not added here: its URL is needed to remove it", 428
            )
        self._stream_call("remove", self._envelope("camera_remove", camera_id, camera_id, url))
        # DeepStream can answer 200 without removing anything: check.
        for _ in range(REMOVE_CHECKS):
            if all(s["id"] != camera_id for s in self.streams()):
                self.urls.set(camera_id, None)
                return {"removed": camera_id}
            self._sleep(0.25)
        raise BackendError(f"camera '{camera_id}' is still there: check its URL", 409)


class Sampler:
    """Background thread: one overview + hardware sample every SAMPLE_S, with history."""

    def __init__(
        self,
        client: RTVisionClient,
        hardware: HardwareMonitor,
        clock: Callable[[], float] = time.time,
    ) -> None:
        self._client = client
        self._hardware = hardware
        self._clock = clock
        self._lock = threading.Lock()
        self._history: deque[dict[str, Any]] = deque(maxlen=HISTORY)
        self._overview: dict[str, Any] = {"online": False, "error": "starting"}
        self._hw: dict[str, Any] = {}
        # time, detections per camera, published events
        self._prev: tuple[float, dict[str, int], float] | None = None

    def step(self) -> None:
        hw = self._hardware.sample()
        try:
            ov: dict[str, Any] = {"online": True, "error": None, **self._client.overview()}
        except (BackendError, KeyError, TypeError, ValueError) as exc:
            ov = {"online": False, "error": str(exc)}
        now = self._clock()
        gpu = hw["gpus"][0] if hw.get("gpus") else {}
        point: dict[str, Any] = {
            "t": round(now, 1),
            "cpu": hw["cpu"]["percent"],
            "ram": _percent(hw["memory"]["used"], hw["memory"]["total"]),
            "gpu": gpu.get("utilization"),
            "vram": _percent(gpu.get("memory_used_mib"), gpu.get("memory_total_mib")),
            "fps": None,
            "streams": {},
            "detections_s": None,
            "events_s": None,
        }
        if ov["online"]:
            point["fps"] = round(sum(s["fps"] for s in ov["streams"]), 1)
            point["streams"] = {s["id"]: round(s["fps"], 1) for s in ov["streams"]}
            detections = {s["id"]: s["detections_total"] for s in ov["streams"]}
            published = ov["totals"].get("rtvision_events_published_total", 0.0)
            if self._prev and now > self._prev[0]:
                dt = now - self._prev[0]
                # per camera: cameras come and go, and counters restart with the service
                new = sum(
                    max(n - self._prev[1][cam], 0)
                    for cam, n in detections.items()
                    if cam in self._prev[1]
                )
                point["detections_s"] = round(new / dt, 1)
                point["events_s"] = round(max(published - self._prev[2], 0) / dt, 2)
            self._prev = (now, detections, published)
            ov["rates"] = {k: point[k] for k in ("fps", "detections_s", "events_s")}
        else:
            self._prev = None
        with self._lock:
            self._overview, self._hw = ov, hw
            self._history.append(point)

    def run(self) -> None:
        while True:
            started = time.monotonic()
            self.step()
            time.sleep(max(SAMPLE_S - (time.monotonic() - started), 0.2))

    def start(self) -> None:
        threading.Thread(target=self.run, name="ui-sampler", daemon=True).start()

    def overview(self) -> dict[str, Any]:
        with self._lock:
            return self._overview

    def hardware(self) -> dict[str, Any]:
        with self._lock:
            return self._hw

    def history(self) -> dict[str, Any]:
        with self._lock:
            return {"interval_s": SAMPLE_S, "points": list(self._history)}


def _percent(used: float | None, total: float | None) -> float | None:
    return round(100.0 * used / total, 1) if used is not None and total else None
