# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""The serializer golden file (written by src/app/tests/test_serialize.c) and consume_validate."""

from __future__ import annotations

import json
import subprocess
import sys

from conftest import GOLDEN, REPO
from perceptra_tools.schemas import validate_event

GOLDEN_EVENT = GOLDEN / "event_basic.json"
CONSUME = REPO / "tests" / "integration" / "consume_validate.py"


def test_golden_event_is_one_valid_json_line() -> None:
    text = GOLDEN_EVENT.read_text()
    assert text.endswith("\n") and text.count("\n") == 1  # JSON Lines, no pretty-printing
    event = json.loads(text)
    validate_event(event)
    assert event["objects"][0]["bbox"] == {
        "left": 812.4,
        "top": 301.0,
        "width": 96.2,
        "height": 254.7,
    }
    assert event["objects"][1]["track_id"] is None
    assert event["objects"][1]["confidence"] is None


def _consume(stdin: str, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(CONSUME), "--stdin", *args],
        input=stdin,
        capture_output=True,
        text=True,
        check=False,
    )


def test_consume_validate_pass_and_fail() -> None:
    line = GOLDEN_EVENT.read_text()
    ok = _consume(
        line * 3, "--min-events", "3", "--sensor-id", "cam-entrance-01", "--require-objects"
    )
    assert ok.returncode == 0 and ok.stdout.startswith("PASS: 3 valid events"), ok.stdout
    assert _consume(line * 2, "--min-events", "3").returncode == 1
    assert _consume(line, "--min-events", "1", "--sensor-id", "other").returncode == 1
    bad = json.loads(line)
    del bad["sensor"]
    res = _consume(line + json.dumps(bad) + "\n", "--min-events", "5")
    assert res.returncode == 1 and "line 2: invalid event" in res.stdout
    assert _consume("not json\n", "--min-events", "1").returncode == 1
    empty = json.loads(line)
    empty["objects"] = []
    res = _consume((json.dumps(empty) + "\n") * 3, "--min-events", "3", "--require-objects")
    assert res.returncode == 1


def test_consume_validate_monotonic() -> None:
    base = json.loads(GOLDEN_EVENT.read_text())

    def ev(frame: int, ts: str) -> str:
        e = json.loads(json.dumps(base))
        e["frame"]["number"], e["timestamp"] = frame, ts
        return json.dumps(e) + "\n"

    good = ev(30, "2026-11-20T06:30:01.000Z") + ev(60, "2026-11-20T06:30:02.000Z")
    assert _consume(good, "--min-events", "2", "--check-monotonic").returncode == 0
    bad = ev(60, "2026-11-20T06:30:02.000Z") + ev(90, "2026-11-20T06:30:01.000Z")
    res = _consume(bad, "--min-events", "2", "--check-monotonic")
    assert res.returncode == 1 and "not monotonic" in res.stdout


def test_track_stability_script() -> None:
    sys.path.insert(0, str(REPO / "tests" / "integration"))
    import track_stability

    base = json.loads(GOLDEN_EVENT.read_text())
    events = []
    for i, ids in enumerate([[1, 2], [1, 2], [1, 3], [1, 3]]):
        e = json.loads(json.dumps(base))
        e["frame"]["number"] = i * 30
        e["objects"] = [dict(base["objects"][0], track_id=t) for t in ids]
        events.append(e)
    r = track_stability.analyse(events)
    assert r["tracks"] == 3 and r["lifetime_max"] == 4
    assert abs(r["persistence"] - (1 + 0.5 + 1) / 3) < 1e-9
