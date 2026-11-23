# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Web UI backend without GStreamer: metrics, sampling, camera changes, hardware, HTTP security."""

from __future__ import annotations

import http.client
import json
import re
import stat
import threading
from pathlib import Path

import pytest
from conftest import GOLDEN
from rtvision_tools.ui import backend, hardware, preview, server

EVENT = json.loads((GOLDEN / "event_basic.json").read_text())  # 1 person + 1 car

METRICS = """# HELP x y
# TYPE rtvision_stream_fps gauge
rtvision_stream_fps{sensor_id="cam-1"} 30.00
rtvision_frames_processed_total{sensor_id="cam-1"} 900
rtvision_objects_detected_total{sensor_id="cam-1",label="person"} 40
rtvision_objects_detected_total{sensor_id="cam-1",label="a \\"b\\""} 2
rtvision_events_published_total 12
rtvision_model_swaps_total{result="ok"} 3
garbage line
"""


class FakeRTVision:
    """Answers like the ops server and DeepStream's REST server."""

    def __init__(self) -> None:
        self.cams = {"cam-1": ("Gate", 0, "rtsp://u:secret@h/1")}
        self.max_streams = 2
        self.age_ms = 500
        self.metrics = METRICS
        self.down = False
        self.calls: list[str] = []

    def __call__(self, url, method="GET", body=None, timeout=4):
        if self.down:
            raise backend.BackendError("rtvision:9464 is not reachable")
        path = url.split("/", 3)[3]
        self.calls.append(f"{method} {path}")
        answer: object = {}
        status = 200
        if path == "info":
            answer = {"max_streams": self.max_streams, "model": {"name": "m"}}
        elif path == "ready":
            answer = {"status": "ready"}
        elif path == "metrics":
            return 200, self.metrics.encode()
        elif path == "events/latest":
            event = {**EVENT, "sensor": {**EVENT["sensor"], "id": "cam-1"}}
            answer = {"events": [{"age_ms": self.age_ms, "event": event}]}
        elif path.endswith("get-stream-info"):
            infos = [
                {"camera_id": k, "camera_name": n, "source_id": i}
                for k, (n, i, _u) in self.cams.items()
            ]
            answer = {"stream-info": {"stream-info": infos}}
        elif path.endswith("/add"):
            v = json.loads(body)["value"]
            self.cams[v["camera_id"]] = (v["camera_name"], len(self.cams), v["camera_url"])
        elif path.endswith("/remove"):
            v = json.loads(body)["value"]
            known = self.cams.get(v["camera_id"])
            if known and known[2] == v["camera_url"]:
                del self.cams[v["camera_id"]]
            elif v["camera_url"] != "rtsp://lies":  # 200 although nothing was removed
                status, answer = 500, {"reason": "No record found"}
        return status, json.dumps(answer).encode()


@pytest.fixture
def fake() -> FakeRTVision:
    return FakeRTVision()


def make_client(fake, urls=None) -> backend.RTVisionClient:
    return backend.RTVisionClient(
        "http://o:1", "http://r:2", fetch=fake, urls=urls, sleep=lambda _s: None
    )


def test_parse_prometheus() -> None:
    rows = backend.parse_prometheus(METRICS)
    assert ("rtvision_stream_fps", {"sensor_id": "cam-1"}, 30.0) in rows
    assert ("rtvision_events_published_total", {}, 12.0) in rows
    assert any(labels.get("label") == 'a "b"' for _n, labels, _v in rows)
    assert len(rows) == 6


def test_overview(fake) -> None:
    ov = make_client(fake).overview()
    s = ov["streams"][0]
    assert (s["id"], s["name"], s["index"], s["fps"]) == ("cam-1", "Gate", 0, 30.0)
    assert s["frames"] == 900 and s["detections_total"] == 42
    assert s["objects"] == {"car": 1, "person": 1}
    assert ov["ready"] and ov["tiler"] == {"rows": 1, "cols": 2}
    assert ov["totals"]["rtvision_model_swaps_total_ok"] == 3
    assert "secret" not in json.dumps(ov)  # camera URLs never reach the browser


def test_old_events_mean_nothing_in_view(fake) -> None:
    fake.age_ms = backend.EVENT_MAX_AGE_MS + 1
    s = make_client(fake).overview()["streams"][0]
    assert s["objects"] == {} and s["event_age_ms"] == fake.age_ms


def test_add_camera(fake) -> None:
    c = make_client(fake)
    assert c.add_stream({"id": "cam-2", "url": "rtsp://h/2"}) == {"added": "cam-2"}
    assert fake.cams["cam-2"] == ("cam-2", 1, "rtsp://h/2")


@pytest.mark.parametrize(
    ("data", "status"),
    [
        ({"id": "cam-1", "url": "rtsp://h/1"}, 409),  # duplicate
        ({"id": "bad id", "url": "rtsp://h/1"}, 400),
        ({"id": "x", "url": "javascript:alert(1)"}, 400),
        ({"id": "x", "url": "rtsp://h/a b"}, 400),
        ({"id": "x"}, 400),
        ([], 400),
    ],
)
def test_add_camera_rejected(fake, data, status: int) -> None:
    with pytest.raises(backend.BackendError) as e:
        make_client(fake).add_stream(data)
    assert e.value.status == status
    assert not any("add" in c for c in fake.calls)


def test_add_camera_when_full(fake) -> None:
    fake.max_streams = 1
    with pytest.raises(backend.BackendError, match="slots are in use"):
        make_client(fake).add_stream({"id": "x", "url": "rtsp://h/x"})


def test_remove_needs_the_url_and_checks_the_result(fake, tmp_path: Path) -> None:
    urls = backend.CameraUrls(tmp_path / "cams.json")
    c = make_client(fake, urls)
    with pytest.raises(backend.BackendError) as e:
        c.remove_stream("cam-1")  # added elsewhere
    assert e.value.status == 428
    with pytest.raises(backend.BackendError, match="still there"):
        c.remove_stream("cam-1", "rtsp://lies")
    assert c.remove_stream("cam-1", "rtsp://u:secret@h/1") == {"removed": "cam-1"}
    c.add_stream({"id": "cam-2", "url": "rtsp://h/2"})
    again = make_client(fake, backend.CameraUrls(tmp_path / "cams.json"))  # UI restarted
    assert again.remove_stream("cam-2") == {"removed": "cam-2"}
    assert json.loads((tmp_path / "cams.json").read_text()) == {}
    assert stat.S_IMODE((tmp_path / "cams.json").stat().st_mode) == 0o600


class FakeHardware:
    def sample(self) -> dict:
        return {
            "cpu": {"percent": 12.5, "per_core": [], "cores": 1, "load": []},
            "memory": {"total": 100, "used": 40},
            "gpus": [{"utilization": 30.0, "memory_used_mib": 1.0, "memory_total_mib": 4.0}],
        }


def test_sampler_rates_per_camera(fake) -> None:
    now = [100.0]
    s = backend.Sampler(make_client(fake), FakeHardware(), clock=lambda: now[0])
    s.step()
    assert s.overview()["online"] and s.history()["points"][-1]["detections_s"] is None
    now[0] += 2
    fake.metrics = METRICS.replace('label="person"} 40', 'label="person"} 60')
    s.step()
    p = s.history()["points"][-1]
    assert p["detections_s"] == 10.0 and p["fps"] == 30.0
    assert (p["cpu"], p["ram"], p["gpu"], p["vram"]) == (12.5, 40.0, 30.0, 25.0)
    # a camera disappearing or appearing must not look like a drop or a burst
    now[0] += 2
    cam = fake.cams.pop("cam-1")
    s.step()
    assert s.history()["points"][-1]["detections_s"] == 0
    now[0] += 2
    fake.cams["cam-1"] = cam
    s.step()
    assert s.history()["points"][-1]["detections_s"] == 0
    # RTVision goes away: offline with a reason, hardware still sampled
    fake.down = True
    s.step()
    assert s.overview()["online"] is False and "not reachable" in s.overview()["error"]
    assert s.history()["points"][-1]["fps"] is None and s.hardware()["cpu"]["percent"] == 12.5


def test_hardware_parsing() -> None:
    gpu = hardware.parse_gpus(
        "0, RTX, 595.99, 25, 1, [N/A], 900, 12227, 60, 40.5, [N/A]\nbad,line\n"
    )
    assert len(gpu) == 1
    assert gpu[0]["utilization"] == 25 and gpu[0]["decoder"] is None and gpu[0]["power_w"] == 40.5
    a = hardware.parse_cpu_times("cpu  10 0 10 80 0 0 0 0\ncpu0 10 0 10 80 0 0 0 0\nintr 1\n")
    b = hardware.parse_cpu_times("cpu  30 0 20 130 20 0 0 0\ncpu0 10 0 10 180 0 0 0 0\n")
    assert hardware.cpu_percent(a, b) == [30.0, 0.0]
    mem = hardware.parse_meminfo(
        "MemTotal: 100 kB\nMemAvailable: 40 kB\nSwapTotal: 10 kB\nSwapFree: 4 kB\nJunk\n"
    )
    assert mem["used"] == 60 * 1024 and mem["swap_used"] == 6 * 1024
    assert hardware.cpu_model("a: b\nmodel name\t: X 9\n") == "X 9"


def test_hardware_monitor_on_this_machine() -> None:
    sample = hardware.HardwareMonitor().sample()
    assert sample["memory"]["total"] > 0 and sample["disk"]["total"] > 0
    assert isinstance(sample["gpus"], list)


@pytest.mark.parametrize(
    ("host", "ok"),
    [
        ("localhost:8080", True),
        ("127.0.0.1:8080", True),
        ("[::1]:8080", True),
        ("192.168.1.5", True),
        ("demo.lan:8080", True),
        ("evil.example", False),
        ("localhost.evil.example", False),
        ("", False),
        (None, False),
    ],
)
def test_host_header(host, ok: bool) -> None:
    assert server.host_allowed(host, frozenset({"demo.lan"})) is ok


@pytest.mark.parametrize(
    ("method", "path", "ok"),
    [
        ("GET", "/api/status", True),
        ("PUT", "/api/videos/a b.mp4", True),
        ("DELETE", "/api/videos/a.mp4", True),
        ("PUT", "/api/videos/../config", False),
        ("DELETE", "/api/config", False),
        ("GET", "/other", False),
    ],
)
def test_replicator_routes(method: str, path: str, ok: bool) -> None:
    assert server.replicator_route_allowed(method, path) is ok


@pytest.fixture
def ui(fake):
    client = make_client(fake)
    sampler = backend.Sampler(client, FakeHardware())
    sampler.step()
    app = server.App(sampler, client, None, None)
    srv = server.Server(("127.0.0.1", 0), server.make_handler(app))
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    def request(method: str, path: str, body=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", srv.server_port, timeout=5)
        data = json.dumps(body).encode() if body is not None else None
        conn.request(method, path, data, headers or {})
        resp = conn.getresponse()
        return resp.status, resp.read(), resp.headers

    yield request
    srv.shutdown()


OK = {"X-RTVision": "1"}


def test_pages_and_assets(ui) -> None:
    for page in ("/", "/live", "/performance", "/replicator"):
        status, body, headers = ui("GET", page)
        assert status == 200 and b"RTVision" in body
        assert "default-src 'self'" in headers["Content-Security-Policy"]
    index = ui("GET", "/")[1].decode()
    for ref in re.findall(r'(?:src|href)="(/static/[^"]+)"', index):
        assert ui("GET", ref)[0] == 200, ref
    assert "http://" not in index and "https://" not in index  # works offline
    assert 'style="' not in index and "<script>" not in index  # allowed by the content policy
    for path in ("/static/../server.py", "/static/%2e%2e/server.py", "/static/x.js", "/nope"):
        assert ui("GET", path)[0] == 404, path


def test_api_and_cross_site_protection(ui, fake) -> None:
    status, body, _ = ui("GET", "/api/overview")
    assert status == 200 and json.loads(body)["streams"][0]["id"] == "cam-1"
    assert ui("GET", "/api/hardware")[0] == 200 and ui("GET", "/api/history")[0] == 200
    cam = {"id": "cam-2", "url": "rtsp://h/2"}
    assert ui("POST", "/api/streams", cam)[0] == 403  # no X-RTVision header
    assert ui("POST", "/api/streams", cam, {**OK, "Host": "evil.example"})[0] == 403
    assert ui("GET", "/api/overview", None, {"Host": "evil.example"})[0] == 403
    assert ui("POST", "/api/streams", cam, OK)[0] == 201
    assert ui("DELETE", "/api/streams/cam-2", None, OK)[0] == 200
    assert ui("DELETE", "/api/streams/cam-1", None, OK)[0] == 428
    assert ui("DELETE", "/api/streams/cam-1", {"url": "rtsp://u:secret@h/1"}, OK)[0] == 200


def test_optional_parts_are_reported(ui) -> None:
    assert ui("GET", "/preview.mjpg")[0] == 503
    assert ui("GET", "/api/replicator/status")[0] == 503


def test_preview_url_cannot_inject_pipeline_elements() -> None:
    assert "rtsp://h:8554/x" in preview.pipeline_description("rtsp://h:8554/x", 15, 80)
    with pytest.raises(ValueError):
        preview.pipeline_description("rtsp://h/x ! filesink location=/tmp/x", 15, 80)
