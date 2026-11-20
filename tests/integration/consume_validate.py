# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Validate Perceptra events (JSON Lines) against schemas/detection_event.v1.schema.json.

    docker logs -f perceptra 2>/dev/null | \\
        python3 tests/integration/consume_validate.py --stdin --min-events 3 \\
            --sensor-id it-cam-0 --require-objects

Reads one event per line from stdin (Kafka consumption is added in M7 by piping
kafka-console-consumer into --stdin). Every line must be a valid event; extra checks:
--sensor-id (every event), --require-objects (at least one event has objects),
--check-monotonic (per stream: frame numbers increase, timestamps never go back). Exits 0 with
"PASS:" as soon as --min-events valid events were seen and the checks hold, otherwise prints
"FAIL:" and exits 1 at EOF or after --timeout seconds.
"""

from __future__ import annotations

import argparse
import json
import select
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from perceptra_tools.schemas import ConfigError, validate_event  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stdin", action="store_true", required=True, help="read events from stdin")
    ap.add_argument("--min-events", type=int, default=3)
    ap.add_argument("--sensor-id")
    ap.add_argument("--require-objects", action="store_true")
    ap.add_argument(
        "--check-monotonic",
        action="store_true",
        help="per sensor.id: frame.number increasing and timestamp non-decreasing",
    )
    ap.add_argument("--timeout", type=float, default=0, help="seconds (0 = until EOF)")
    args = ap.parse_args(argv)

    deadline = time.monotonic() + args.timeout if args.timeout > 0 else None
    valid = with_objects = 0
    lineno = 0
    last: dict[str, tuple[int, str]] = {}  # sensor.id -> (frame.number, timestamp)
    stream = sys.stdin

    def done() -> bool:
        return valid >= args.min_events and (with_objects > 0 or not args.require_objects)

    while True:
        if deadline is not None:
            left = deadline - time.monotonic()
            if left <= 0:
                break
            ready, _, _ = select.select([stream], [], [], left)
            if not ready:
                break
        line = stream.readline()
        if not line:
            break  # EOF
        lineno += 1
        line = line.strip()
        if not line:
            continue
        try:
            event = json.loads(line)
            validate_event(event)
        except (json.JSONDecodeError, ConfigError) as exc:
            print(f"FAIL: line {lineno}: invalid event: {exc}\n  {line[:300]}")
            return 1
        if args.sensor_id and event["sensor"]["id"] != args.sensor_id:
            print(f"FAIL: line {lineno}: sensor.id {event['sensor']['id']!r} != {args.sensor_id!r}")
            return 1
        if args.check_monotonic:
            sid, cur = event["sensor"]["id"], (event["frame"]["number"], event["timestamp"])
            prev = last.get(sid)
            # RFC 3339 UTC strings with fixed width compare chronologically as strings.
            if prev and (cur[0] <= prev[0] or cur[1] < prev[1]):
                print(f"FAIL: line {lineno}: {sid}: not monotonic: {prev} -> {cur}")
                return 1
            last[sid] = cur
        valid += 1
        with_objects += 1 if event["objects"] else 0
        if done():
            print(f"PASS: {valid} valid events ({with_objects} with objects)")
            return 0

    print(
        f"FAIL: {valid} valid events ({with_objects} with objects); need {args.min_events}"
        + (" with objects" if args.require_objects else "")
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
