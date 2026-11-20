# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Example consumer: print Perceptra events as one readable line each.

Reads `perceptra.detection.v1` events as JSON Lines on stdin (standard library only):

    docker compose -f deploy/compose/compose.yaml exec -T kafka \\
        /opt/kafka/bin/kafka-console-consumer.sh --bootstrap-server kafka:9092 \\
        --topic perceptra.rfdetr-s-coco | python3 examples/consumer/print_events.py

    2026-11-20T14:10:41.487Z it-cam-0 frame=30 objects=11 car=6 person=3 backpack=1 bus=1

--objects also prints one line per object (track id, label, confidence, box in source pixels).
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter


def summary(event: dict) -> str:
    objects = event.get("objects", [])
    counts = Counter(o.get("label", "?") for o in objects)
    parts = [
        event.get("timestamp", "?"),
        event.get("sensor", {}).get("id", "?"),
        f"frame={event.get('frame', {}).get('number', '?')}",
        f"objects={len(objects)}",
    ] + [f"{label}={n}" for label, n in counts.most_common()]
    return " ".join(parts)


def object_line(o: dict) -> str:
    b = o.get("bbox", {})
    track = "-" if o.get("track_id") is None else o["track_id"]
    conf = "-" if o.get("confidence") is None else f"{o['confidence']:.2f}"
    return (
        f"    track={track} {o.get('label', '?')} conf={conf} "
        f"box=({b.get('left', 0):.0f},{b.get('top', 0):.0f} "
        f"{b.get('width', 0):.0f}x{b.get('height', 0):.0f})"
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--objects", action="store_true", help="also print every object")
    args = ap.parse_args(argv)
    for line in sys.stdin:
        line = line.strip()
        if not line.startswith("{"):
            continue  # consumer chatter, blank lines
        try:
            event = json.loads(line)
        except ValueError as e:
            print(f"skipping malformed line: {e}", file=sys.stderr)
            continue
        print(summary(event))
        if args.objects:
            for o in event.get("objects", []):
                print(object_line(o))
        sys.stdout.flush()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyboardInterrupt, BrokenPipeError):
        sys.exit(0)
