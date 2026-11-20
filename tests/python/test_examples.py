# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""examples/consumer: occupancy smoothing/staleness and the event printer."""

from __future__ import annotations

import copy
import importlib.util
import io
import json
import sys

import pytest
from conftest import GOLDEN, REPO

CONSUMERS = REPO / "examples" / "consumer"
EVENT = json.loads((GOLDEN / "event_basic.json").read_text())  # 1 person + 1 car


def _load(name: str):
    spec = importlib.util.spec_from_file_location(name, CONSUMERS / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod  # dataclasses need the module registered
    spec.loader.exec_module(mod)
    return mod


occupancy = _load("occupancy")
print_events = _load("print_events")


def event(sensor: str, people: int, others: int = 0) -> dict:
    e = copy.deepcopy(EVENT)
    person, car = e["objects"]
    e["sensor"]["id"] = sensor
    e["objects"] = [copy.deepcopy(person) for _ in range(people)] + [
        copy.deepcopy(car) for _ in range(others)
    ]
    return e


def test_counts_only_the_label_and_reports_changes() -> None:
    t = occupancy.OccupancyTracker(window=1)
    assert t.update(event("a", 2, others=3), now=0) == ("a", 2)
    assert t.update(event("a", 2, others=1), now=1) is None  # unchanged count: silent
    assert t.update(event("a", 4), now=2) == ("a", 4)
    assert t.update(event("b", 1), now=2) == ("b", 1)  # per camera


def test_median_smooths_flicker() -> None:
    t = occupancy.OccupancyTracker(window=3)
    assert t.update(event("a", 3), 0) == ("a", 3)
    assert t.update(event("a", 0), 1) is None  # one missed detection: median_high(3, 0) = 3
    assert t.update(event("a", 3), 2) is None  # median of 3, 0, 3 = 3


def test_min_confidence_and_null_confidence() -> None:
    e = event("a", 2)
    e["objects"][0]["confidence"] = 0.2
    e["objects"][1]["confidence"] = None  # tracker-only object: counted
    assert occupancy.OccupancyTracker(min_confidence=0.5).count(e) == 1


def test_stale_camera_drops_to_zero_once() -> None:
    t = occupancy.OccupancyTracker(window=1, stale_s=5)
    t.update(event("a", 2), now=0)
    assert t.expire(now=4.9) == []
    assert t.expire(now=5.0) == [("a", 0)]
    assert t.expire(now=9.0) == []  # reported once
    assert t.update(event("a", 1), now=10) == ("a", 1)


def test_occupancy_main_reads_jsonl(monkeypatch, capsys) -> None:
    lines = [json.dumps(event("cam-1", n)) for n in (2, 2, 2)] + ["not json", ""]
    monkeypatch.setattr(sys, "stdin", io.StringIO("\n".join(lines) + "\n"))
    monkeypatch.setattr(occupancy.select, "select", lambda r, w, x, t: (r, [], []))
    assert occupancy.main(["--json", "--window", "1"]) == 0
    out = [json.loads(x) for x in capsys.readouterr().out.splitlines()]
    assert out == [
        {"sensor_id": "cam-1", "timestamp": EVENT["timestamp"], "label": "person", "count": 2}
    ]


@pytest.mark.parametrize("flag", [[], ["--objects"]])
def test_print_events(monkeypatch, capsys, flag: list[str]) -> None:
    monkeypatch.setattr(sys, "stdin", io.StringIO("chatter\n" + json.dumps(EVENT) + "\n"))
    assert print_events.main(flag) == 0
    out = capsys.readouterr().out.splitlines()
    assert out[0] == "2026-11-20T06:30:00.123Z cam-entrance-01 frame=1230 objects=2 person=1 car=1"
    if flag:
        assert out[1] == "    track=17 person conf=0.87 box=(812,301 96x255)"
        assert out[2] == "    track=- car conf=- box=(0,10 300x120)"
