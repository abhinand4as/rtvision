# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Example consumer: per-camera person count from Perceptra events.

Reads `perceptra.detection.v1` events as JSON Lines on stdin (standard library only) and prints a
line whenever a camera's smoothed count changes:

    docker compose -f deploy/compose/compose.yaml exec -T kafka \\
        /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server kafka:9092 \\
        --topic people-raw | python3 examples/consumer/occupancy.py

    2026-11-20T14:10:41.487Z  it-cam-0  people=5

Detections flicker from frame to frame, so the count is the upper median of the last --window
events of that camera (a missed detection lowers a count far more often than a false positive
raises it). Perceptra does not publish frames without objects by default
(publish.include_empty_frames: false), so a camera with no event for --stale-s seconds is
reported as 0.

A production consumer would read Kafka directly with a client library; this example keeps the
repository free of extra dependencies.
"""

from __future__ import annotations

import argparse
import json
import select
import statistics
import sys
import time
from collections import deque
from dataclasses import dataclass, field


@dataclass
class _Camera:
    counts: deque[int]
    last_seen: float
    reported: int | None = None


@dataclass
class OccupancyTracker:
    """Smoothed per-camera count of objects with a given label."""

    label: str = "person"
    window: int = 5
    stale_s: float = 5.0
    min_confidence: float = 0.0
    cameras: dict[str, _Camera] = field(default_factory=dict)

    def count(self, event: dict) -> int:
        return sum(
            1
            for o in event.get("objects", [])
            if o.get("label") == self.label
            and (o.get("confidence") is None or o["confidence"] >= self.min_confidence)
        )

    def update(self, event: dict, now: float) -> tuple[str, int] | None:
        """Adds one event; returns (sensor id, smoothed count) if that camera's count changed."""
        sensor = event["sensor"]["id"]
        cam = self.cameras.get(sensor)
        if cam is None:
            cam = self.cameras[sensor] = _Camera(deque(maxlen=self.window), now)
        cam.counts.append(self.count(event))
        cam.last_seen = now
        return self._report(sensor, cam, int(statistics.median_high(cam.counts)))

    def expire(self, now: float) -> list[tuple[str, int]]:
        """Cameras without events for stale_s seconds drop to 0 (empty frames are not published)."""
        changes = []
        for sensor, cam in self.cameras.items():
            if now - cam.last_seen >= self.stale_s and cam.reported != 0:
                cam.counts.clear()
                change = self._report(sensor, cam, 0)
                if change:
                    changes.append(change)
        return changes

    @staticmethod
    def _report(sensor: str, cam: _Camera, value: int) -> tuple[str, int] | None:
        if value == cam.reported:
            return None
        cam.reported = value
        return sensor, value


def _emit(sensor: str, count: int, timestamp: str, label: str, as_json: bool) -> None:
    if as_json:
        print(
            json.dumps(
                {"sensor_id": sensor, "timestamp": timestamp, "label": label, "count": count}
            )
        )
    else:
        name = "people" if label == "person" else label
        print(f"{timestamp}  {sensor}  {name}={count}")
    sys.stdout.flush()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--label", default="person", help="object label to count (default: person)")
    ap.add_argument("--window", type=int, default=5, help="events per camera for the median")
    ap.add_argument("--stale-s", type=float, default=5.0, help="seconds without events -> 0")
    ap.add_argument("--min-confidence", type=float, default=0.0)
    ap.add_argument("--json", action="store_true", help="print JSON Lines instead of text")
    args = ap.parse_args(argv)
    if args.window < 1:
        ap.error("--window must be >= 1")

    tracker = OccupancyTracker(args.label, args.window, args.stale_s, args.min_confidence)
    stream = sys.stdin
    while True:
        ready, _, _ = select.select([stream], [], [], 1.0)
        now = time.monotonic()
        if ready:
            line = stream.readline()
            if not line:
                break  # EOF
            line = line.strip()
            if not line.startswith("{"):
                continue  # consumer chatter, blank lines
            try:
                event = json.loads(line)
                change = tracker.update(event, now)
            except (ValueError, KeyError, TypeError) as e:
                print(f"skipping malformed event: {e}", file=sys.stderr)
                continue
            if change:
                _emit(*change, event.get("timestamp", ""), args.label, args.json)
        for sensor, count in tracker.expire(now):
            stamp = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            _emit(sensor, count, stamp, args.label, args.json)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
