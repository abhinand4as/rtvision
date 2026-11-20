# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Summarise tracker stability from Perceptra events (JSON Lines on stdin or a file).

Per stream (sensor.id), events are taken in frame order. Reported:
  * persistence: mean fraction of track ids of event i that are present again in event i+1
  * lifetime: number of consecutive events each track id is seen in (median, max)
  * untracked: objects with track_id null
Exit 1 if persistence < --min-persistence or median lifetime < --min-lifetime.

    docker logs perceptra 2>/dev/null | python3 tests/integration/track_stability.py
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import defaultdict


def analyse(events: list[dict]) -> dict:
    by_sensor: dict[str, list[dict]] = defaultdict(list)
    for e in events:
        by_sensor[e["sensor"]["id"]].append(e)
    persist, lifetimes, untracked, objects = [], [], 0, 0
    for evs in by_sensor.values():
        evs.sort(key=lambda e: e["frame"]["number"])
        ids = []
        for e in evs:
            objects += len(e["objects"])
            untracked += sum(1 for o in e["objects"] if o["track_id"] is None)
            ids.append({o["track_id"] for o in e["objects"] if o["track_id"] is not None})
        for a, b in zip(ids, ids[1:], strict=False):
            if a:
                persist.append(len(a & b) / len(a))
        run: dict[int, int] = {}
        for cur in ids:
            for tid in list(run):
                if tid not in cur:
                    lifetimes.append(run.pop(tid))
            for tid in cur:
                run[tid] = run.get(tid, 0) + 1
        lifetimes.extend(run.values())
    return {
        "streams": len(by_sensor),
        "events": len(events),
        "objects": objects,
        "untracked": untracked,
        "tracks": len(lifetimes),
        "persistence": statistics.fmean(persist) if persist else 0.0,
        "lifetime_median": statistics.median(lifetimes) if lifetimes else 0,
        "lifetime_max": max(lifetimes) if lifetimes else 0,
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("file", nargs="?", help="JSON Lines file (default: stdin)")
    ap.add_argument("--min-persistence", type=float, default=0.5)
    ap.add_argument("--min-lifetime", type=float, default=2)
    args = ap.parse_args(argv)
    fh = open(args.file, encoding="utf-8") if args.file else sys.stdin  # noqa: SIM115
    events = [json.loads(line) for line in fh if line.strip().startswith("{")]
    r = analyse(events)
    print(
        f"streams={r['streams']} events={r['events']} objects={r['objects']} "
        f"untracked={r['untracked']} tracks={r['tracks']} persistence={r['persistence']:.2f} "
        f"lifetime_median={r['lifetime_median']} lifetime_max={r['lifetime_max']}"
    )
    ok = r["persistence"] >= args.min_persistence and r["lifetime_median"] >= args.min_lifetime
    print("PASS: tracks are stable" if ok else "FAIL: tracks are not stable enough")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
