# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""RTSP replicator without GStreamer: configuration, video library, control API, CLI options."""

from __future__ import annotations

import http.client
import io
import json
import stat
import subprocess
from pathlib import Path

import pytest
from conftest import REPO
from rtvision_tools.replicator import api, cli
from rtvision_tools.replicator.config import (
    ReplicatorConfig,
    config_from_videos,
    load_config,
    parse_config,
    save_config,
)
from rtvision_tools.replicator.library import LibraryError, VideoLibrary, safe_name
from rtvision_tools.schemas import ConfigError


def test_defaults_and_roundtrip(tmp_path: Path) -> None:
    cfg = parse_config({"streams": [{"name": "lobby", "video": "a b.mp4"}]})
    s = cfg.streams[0]
    assert (s.loop, s.start_offset_s, s.enabled, s.path) == (True, 0, True, "/lobby")
    assert cfg.rtsp_port == 8554 and cfg.public_host is None
    save_config(cfg, tmp_path / "sub" / "r.yaml")
    assert load_config(tmp_path / "sub" / "r.yaml") == cfg
    assert [p.name for p in (tmp_path / "sub").iterdir()] == ["r.yaml"]  # no temporary file left


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ({"streams": [{"name": "a b", "video": "x.mp4"}]}, r"streams\[0\]\.name"),
        ({"streams": [{"name": "a", "video": "../x.mp4"}]}, r"streams\[0\]\.video"),
        ({"streams": [{"name": "a", "video": "x.mp4", "speed": 2}]}, "unknown key"),
        ({"streams": [{"name": "a"}]}, "'video' is a required"),
        ({"streams": [{"name": "a", "video": "x.mp4"}] * 2}, "duplicate stream name 'a'"),
        ({"rtsp_port": 0}, "rtsp_port"),
        ({"public_host": "bad host"}, "public_host"),
        ([], "<root>"),
    ],
)
def test_invalid_config(data, message: str) -> None:
    with pytest.raises(ConfigError, match=message):
        parse_config(data)


def test_config_from_videos_round_robin_with_stagger() -> None:
    cfg = config_from_videos(["a.mp4", "b.mp4"], count=5, stagger_s=4)
    assert [(s.name, s.video, s.start_offset_s) for s in cfg.streams] == [
        ("cam-1", "a.mp4", 0),
        ("cam-2", "b.mp4", 0),
        ("cam-3", "a.mp4", 4),
        ("cam-4", "b.mp4", 4),
        ("cam-5", "a.mp4", 8),
    ]
    assert len(config_from_videos(["a.mp4", "b.mp4"]).streams) == 2
    with pytest.raises(ConfigError):
        config_from_videos([])
    with pytest.raises(ConfigError):
        config_from_videos(["a.mp4"], count=0)


@pytest.mark.parametrize(
    "name", ["../x.mp4", "a/b.mp4", ".hidden.mp4", "x.sh", "a..b.mp4", "é.mp4", ""]
)
def test_unsafe_names(name: str) -> None:
    with pytest.raises(LibraryError):
        safe_name(name)


def ok_probe(path: Path) -> dict:
    return {"codec": "h264"} if b"video" in path.read_bytes() else {"error": "not a video"}


def test_upload_list_delete(tmp_path: Path) -> None:
    lib = VideoLibrary(tmp_path, probe=ok_probe)
    assert lib.list() == []
    info = lib.store("street cam.mp4", io.BytesIO(b"video data"), 10)
    assert info == {"name": "street cam.mp4", "size_bytes": 10, "codec": "h264"}
    (tmp_path / "replicator.yaml").write_text("x")  # not a video: ignored
    assert lib.names() == ["street cam.mp4"]
    lib.delete("street cam.mp4")
    with pytest.raises(LibraryError) as e:
        lib.delete("street cam.mp4")
    assert e.value.status == 404


@pytest.mark.parametrize(
    ("body", "length", "status"),
    [(b"junk", 4, 415), (b"video", 10, 400), (b"", 0, 411), (b"video!", 2000, 413)],
)
def test_rejected_uploads_leave_nothing_behind(tmp_path, body, length, status) -> None:
    lib = VideoLibrary(tmp_path, probe=ok_probe, max_upload=1000)
    with pytest.raises(LibraryError) as e:
        lib.store("a.mp4", io.BytesIO(body), length)
    assert e.value.status == status
    assert list(tmp_path.iterdir()) == []


def test_failed_replacement_keeps_the_old_video(tmp_path: Path) -> None:
    lib = VideoLibrary(tmp_path, probe=ok_probe)
    lib.store("a.mp4", io.BytesIO(b"video 1"), 7)
    with pytest.raises(LibraryError):
        lib.store("a.mp4", io.BytesIO(b"junk"), 4)
    assert (tmp_path / "a.mp4").read_bytes() == b"video 1"


class FakeEngine:
    def __init__(self) -> None:
        self.config = ReplicatorConfig()
        self.running = True

    def call(self, func):
        return func()

    def apply(self, cfg: ReplicatorConfig) -> None:
        if cfg.rtsp_port != self.config.rtsp_port:
            raise ValueError("rtsp_port cannot be changed while running")
        self.config = cfg

    def set_running(self, running: bool) -> None:
        self.running = running

    def status(self) -> dict:
        return {"running": self.running, "streams": [s.name for s in self.config.streams]}


@pytest.fixture
def server(tmp_path: Path):
    ctl = api.Controller(
        FakeEngine(), VideoLibrary(tmp_path / "v", probe=ok_probe), tmp_path / "r.yaml"
    )
    srv = api.serve(ctl, "127.0.0.1", 0)

    def request(method: str, path: str, body=None):
        conn = http.client.HTTPConnection("127.0.0.1", srv.server_port, timeout=5)
        data = body if isinstance(body, bytes) or body is None else json.dumps(body).encode()
        conn.request(method, path, data)
        resp = conn.getresponse()
        return resp.status, json.loads(resp.read())

    yield request, tmp_path
    srv.shutdown()


def test_control_api(server) -> None:
    request, tmp = server
    assert request("GET", "/api/status") == (200, {"running": True, "streams": []})
    status, info = request("PUT", "/api/videos/a%20b.mp4", b"video")
    assert status == 201 and info["name"] == "a b.mp4"
    cfg = {"streams": [{"name": "cam-1", "video": "a b.mp4"}]}
    assert request("PUT", "/api/config", cfg) == (200, {"running": True, "streams": ["cam-1"]})
    assert load_config(tmp / "r.yaml").streams[0].video == "a b.mp4"  # saved
    assert request("DELETE", "/api/videos/a%20b.mp4")[0] == 409  # in use
    assert request("POST", "/api/stop")[1]["running"] is False
    assert request("PUT", "/api/config", {"streams": []})[0] == 200
    assert request("DELETE", "/api/videos/a%20b.mp4")[0] == 200


@pytest.mark.parametrize(
    ("method", "path", "body", "status"),
    [
        ("PUT", "/api/config", {"streams": [{"name": "x", "video": "missing.mp4"}]}, 400),
        ("PUT", "/api/config", {"streams": 1}, 400),
        ("PUT", "/api/config", b"{not json", 400),
        ("PUT", "/api/config", {"rtsp_port": 9999}, 400),
        ("PUT", "/api/videos/..%2f..%2fx.mp4", b"video", 400),
        ("PUT", "/api/videos/x.exe", b"video", 400),
        ("PUT", "/api/videos/x.mp4", b"junk", 415),
        ("GET", "/api/nope", None, 404),
        ("POST", "/api/config", {}, 404),
    ],
)
def test_control_api_errors(server, method, path, body, status) -> None:
    request, _ = server
    got, answer = request(method, path, body)
    assert got == status and answer["error"]


def test_cli_resolve(tmp_path: Path) -> None:
    video = tmp_path / "a.mp4"
    video.write_bytes(b"video")

    def resolve(*args: str):
        return cli.resolve(cli.build_parser().parse_args(list(args)))

    cfg, directory = resolve(str(video), "--streams", "3", "--port", "8600", "--prefix", "gate")
    assert [s.name for s in cfg.streams] == ["gate-1", "gate-2", "gate-3"]
    assert cfg.rtsp_port == 8600 and directory == tmp_path
    (tmp_path / "r.yaml").write_text("streams: [{name: x, video: a.mp4}]\n")
    cfg, directory = resolve("--config", str(tmp_path / "r.yaml"), "--public-host", "demo")
    assert cfg.streams[0].name == "x" and cfg.public_host == "demo" and directory == tmp_path
    cfg, _ = resolve("--videos-dir", str(tmp_path), "--api-port", "8090")
    assert cfg.streams == ()
    for bad in ([], [str(tmp_path / "no.mp4")], [str(video), "--config", str(tmp_path / "r.yaml")]):
        with pytest.raises(ConfigError):
            resolve(*bad)


def test_cli_reports_errors(capsys) -> None:
    assert cli.main(["nope.mp4"]) == 2
    assert "nope.mp4" in capsys.readouterr().err


def test_wrapper_script(tmp_path: Path) -> None:
    script = REPO / "tools" / "replicator" / "replicator.sh"
    assert script.stat().st_mode & stat.S_IXUSR
    env = {"PATH": "/usr/bin:/bin", "REPLICATOR_DOCKER": "1", "RTVISION_IMAGE": "no/such:image"}
    out = subprocess.run([script, "x.mp4"], capture_output=True, text=True, env=env, check=False)
    assert out.returncode == 2 and "not found" in out.stderr
