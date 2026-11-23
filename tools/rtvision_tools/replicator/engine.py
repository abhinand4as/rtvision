# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""GStreamer engine of the RTSP replicator.

One RTSP mount per stream. Every viewer gets its own playback that starts at a keyframe, so a
picture appears at once (joining a running playback would wait for the file's next keyframe, which
can be many seconds away). H.264/H.265 is passed through without re-encoding::

    feeder:  filesrc ! parsebin ! h26Xparse ! appsink (sync=true: paced in real time)
    media:   appsrc (live) ! h26Xparse ! rtph26Xpay

The feeder re-stamps each buffer onto one continuous timeline. At end-of-file it seeks back to the
start and continues the timeline, so RTSP clients see an endless stream without discontinuities.

Threading: all public methods of Engine run on the GLib main-loop thread (see `call()`).
"""

from __future__ import annotations

import socket
import threading
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstApp, GstRtspServer  # noqa: E402, F401

from .config import ReplicatorConfig, StreamSpec  # noqa: E402

CODECS = {"video/x-h264": "h264", "video/x-h265": "h265"}
PROBE_TIMEOUT_S = 15
NO_TRACK_WAIT_S = 1.0
MEDIA_START_S = 10
NONE = Gst.CLOCK_TIME_NONE


def init() -> None:
    Gst.init(None)


def probe(path: Path) -> dict[str, Any]:
    """Codec, resolution, fps and duration of a video file. `error` if it cannot be served.

    Parses only (no decoder, no GPU): GstDiscoverer would try to decode audio and video."""
    pipeline = Gst.Pipeline.new(None)
    src = Gst.ElementFactory.make("filesrc")
    src.set_property("location", str(path))
    parsebin = Gst.ElementFactory.make("parsebin")
    pipeline.add(src)
    pipeline.add(parsebin)
    src.link(parsebin)
    video: list[Any] = []
    pads: list[float] = []

    def on_pad(_bin: Any, pad: Any) -> None:
        pads.append(time.monotonic())
        caps = pad.get_current_caps() or pad.query_caps(None)
        if caps.get_size() and caps.get_structure(0).get_name().startswith(("video/", "image/")):
            video.append(caps.get_structure(0).copy())
        sink = Gst.ElementFactory.make("fakesink")
        sink.set_property("sync", False)
        pipeline.add(sink)
        sink.sync_state_with_parent()
        pad.link(sink.get_static_pad("sink"))

    # parsebin reaches PAUSED before it has found the tracks, and its no-more-pads signal is not
    # reliable: wait for the video track, or until no new track appeared for NO_TRACK_WAIT_S.
    parsebin.connect("pad-added", on_pad)
    ok, duration = False, 0
    try:
        pipeline.set_state(Gst.State.PAUSED)
        bus = pipeline.get_bus()
        deadline = time.monotonic() + PROBE_TIMEOUT_S
        msg = None
        while not video and msg is None and time.monotonic() < deadline:
            msg = bus.timed_pop_filtered(20 * Gst.MSECOND, Gst.MessageType.ERROR)
            if pads and time.monotonic() - pads[-1] > NO_TRACK_WAIT_S:
                break
        if not video:
            if msg:
                return {"error": f"cannot read video: {msg.parse_error()[0].message}"}
            if not pads:
                return {"error": "cannot read video: timed out"}
            return {"error": "no video track found"}
        for _ in range(50):
            ok, duration = pipeline.query_duration(Gst.Format.TIME)
            if ok:
                break
            time.sleep(0.02)
    finally:
        pipeline.set_state(Gst.State.NULL)
    caps = video[0]
    media = caps.get_name()
    out: dict[str, Any] = {
        "codec": CODECS.get(media, media),
        "width": caps.get_int("width")[1] or None,
        "height": caps.get_int("height")[1] or None,
        "fps": None,
        "duration_s": round(duration / Gst.SECOND, 2) if ok else None,
    }
    found, num, den = caps.get_fraction("framerate")
    if found and num and den:
        out["fps"] = round(num / den, 2)
    if media not in CODECS:
        out["error"] = f"unsupported codec {media} (only H.264 and H.265 are supported)"
    return out


class Feeder:
    """Reads one file in real time and pushes it into an appsrc, looping forever if asked."""

    def __init__(self, spec: StreamSpec, path: Path, codec: str, appsrc: Any) -> None:
        self.spec = spec
        self.codec = codec
        self.appsrc = appsrc
        self.loops = 0
        self.frames = 0
        self.error: str | None = None
        self._stopped = False
        self._base: int | None = None  # output time of the current pass's first buffer
        self._first: int | None = None  # input time of the current pass's first buffer
        self._last_end = 0  # output time at which the current pass ends so far
        # Start offset: preroll (PAUSED) -> flushing seek -> PLAYING. Samples from before the seek
        # must not go out; the flush-stop event that precedes the new data is the boundary (the
        # keyframe can arrive before the seek is reported as done).
        self._seeking = False
        self._ready = spec.start_offset_s <= 0
        self._started = self._ready

        self.pipeline = Gst.Pipeline.new(f"feeder-{spec.name}")
        src = Gst.ElementFactory.make("filesrc")
        src.set_property("location", str(path))
        parsebin = Gst.ElementFactory.make("parsebin")
        self._parse = Gst.ElementFactory.make(f"{codec}parse")
        self._parse.set_property("config-interval", -1)
        caps = Gst.ElementFactory.make("capsfilter")
        caps.set_property(
            "caps",
            Gst.Caps.from_string(f"video/x-{codec},stream-format=byte-stream,alignment=au"),
        )
        sink = Gst.ElementFactory.make("appsink")
        sink.set_property("emit-signals", True)
        sink.set_property("sync", True)
        sink.set_property("max-buffers", 4)
        for el in (src, parsebin, self._parse, caps, sink):
            self.pipeline.add(el)
        src.link(parsebin)
        self._parse.link(caps)
        caps.link(sink)
        parsebin.connect("pad-added", self._on_pad)
        sink.connect("new-sample", self._on_sample)
        sink.get_static_pad("sink").add_probe(Gst.PadProbeType.EVENT_FLUSH, self._on_flush)
        # Own control thread: feeders are created on an RTSP client thread that blocks until the
        # first frame, and that thread ends with its client while shared streams keep running.
        self._control = threading.Thread(target=self._run, name=f"feeder-{spec.name}", daemon=True)

    def start(self) -> None:
        self._control.start()

    def _run(self) -> None:
        bus = self.pipeline.get_bus()
        kinds = Gst.MessageType.ASYNC_DONE | Gst.MessageType.EOS | Gst.MessageType.ERROR
        self.pipeline.set_state(Gst.State.PLAYING if self._ready else Gst.State.PAUSED)
        while not self._stopped:
            msg = bus.timed_pop_filtered(100 * Gst.MSECOND, kinds)
            if msg is not None and not self._stopped:
                self._on_message(msg)
        self.pipeline.set_state(Gst.State.NULL)

    def stop(self) -> None:
        """Returns at once; the control thread shuts the pipeline down."""
        self._stopped = True

    def _on_pad(self, _bin: Any, pad: Any) -> None:
        caps = pad.get_current_caps() or pad.query_caps(None)
        name = caps.get_structure(0).get_name() if caps.get_size() else ""
        target = self._parse.get_static_pad("sink")
        if name == f"video/x-{self.codec}" and not target.is_linked():
            pad.link(target)
            return
        # Audio, subtitles, extra video: swallow, so an unlinked pad cannot stop the demuxer.
        fake = Gst.ElementFactory.make("fakesink")
        fake.set_property("sync", False)
        fake.set_property("async", False)
        self.pipeline.add(fake)
        fake.sync_state_with_parent()
        pad.link(fake.get_static_pad("sink"))

    def _on_flush(self, _pad: Any, info: Any) -> Any:
        if self._seeking and info.get_event().type == Gst.EventType.FLUSH_STOP:
            self._ready = True
        return Gst.PadProbeReturn.OK

    def _on_sample(self, sink: Any) -> Any:
        sample = sink.emit("pull-sample")
        if sample is None or self._stopped:
            return Gst.FlowReturn.EOS
        if not self._ready:
            return Gst.FlowReturn.OK
        buf = sample.get_buffer().copy()
        stamp = buf.dts if buf.dts != NONE else buf.pts
        if stamp == NONE:
            return Gst.FlowReturn.OK
        if self._first is None:
            now = self._media_time()
            if now is None:
                return Gst.FlowReturn.EOS
            if self._base is None:
                self._base = now
            else:
                # next pass: continue the timeline, never behind the media clock
                self._base = max(self._last_end, now)
            self._first = stamp
            if self.appsrc.get_property("caps") is None:
                self.appsrc.set_property("caps", sample.get_caps())
        shift = self._base - self._first
        if buf.pts != NONE:
            buf.pts = max(buf.pts + shift, 0)
        buf.dts = max(stamp + shift, 0)
        duration = buf.duration if buf.duration != NONE else 0
        self._last_end = max(self._last_end, buf.dts + duration)
        self.frames += 1
        flow = self.appsrc.emit("push-buffer", buf)
        return Gst.FlowReturn.OK if flow == Gst.FlowReturn.OK else Gst.FlowReturn.EOS

    def _media_time(self) -> int | None:
        """Running time of the RTSP media. Waits for it to start: the first buffer is the keyframe
        everything after it depends on, so it must not be dropped."""
        deadline = time.monotonic() + MEDIA_START_S
        while not self._stopped and time.monotonic() < deadline:
            now = self.appsrc.get_current_running_time()
            if now != NONE:
                return now
            time.sleep(0.005)
        return None

    def _seek(self, seconds: float) -> None:
        self.pipeline.seek_simple(
            Gst.Format.TIME,
            Gst.SeekFlags.FLUSH | Gst.SeekFlags.KEY_UNIT,
            int(seconds * Gst.SECOND),
        )

    def _on_message(self, msg: Any) -> None:
        if msg.type == Gst.MessageType.ASYNC_DONE and not self._started:
            self._seeking = True
            ok, duration = self.pipeline.query_duration(Gst.Format.TIME)
            offset = self.spec.start_offset_s
            if ok and duration > 0:
                offset %= duration / Gst.SECOND
            self._seek(offset)
            self._started = True
            self.pipeline.set_state(Gst.State.PLAYING)
        elif msg.type == Gst.MessageType.EOS:
            if not self.spec.loop:
                self.appsrc.emit("end-of-stream")
                return
            self.loops += 1
            self._first = None
            self._seek(0)
        elif msg.type == Gst.MessageType.ERROR:
            err, _debug = msg.parse_error()
            self.error = err.message
            self._stopped = True
            self.appsrc.emit("end-of-stream")


class Mount:
    """One RTSP path: the media factory plus one feeder per viewer."""

    def __init__(self, server: Any, spec: StreamSpec, path: Path, info: dict[str, Any]) -> None:
        self.spec = spec
        self.info = info
        self.last_error: str | None = None
        self._lock = threading.Lock()  # media callbacks run on client threads
        self._feeders: list[Feeder] = []
        self._frames = 0  # of viewers that left
        self._path = path
        self._server = server
        codec = info["codec"]
        self._factory = GstRtspServer.RTSPMediaFactory()
        self._factory.set_shared(False)
        self._factory.set_launch(
            "( appsrc name=src is-live=true format=time do-timestamp=false max-bytes=8388608 "
            f"leaky-type=downstream ! {codec}parse ! rtp{codec}pay name=pay0 pt=96 "
            "config-interval=1 )"
        )
        self._factory.connect("media-configure", self._on_media)
        server.get_mount_points().add_factory(spec.path, self._factory)

    def _on_media(self, _factory: Any, media: Any) -> None:
        appsrc = media.get_element().get_by_name_recurse_up("src")
        feeder = Feeder(self.spec, self._path, self.info["codec"], appsrc)
        with self._lock:
            self._feeders.append(feeder)
        media.connect("unprepared", self._on_unprepared, feeder)
        feeder.start()

    def _on_unprepared(self, _media: Any, feeder: Feeder) -> None:
        feeder.stop()
        with self._lock:
            if feeder in self._feeders:
                self._feeders.remove(feeder)
                self._frames += feeder.frames
                self.last_error = feeder.error or self.last_error

    def remove(self) -> None:
        """Unmounts the path; connected viewers get end-of-stream."""
        self._server.get_mount_points().remove_factory(self.spec.path)
        with self._lock:
            feeders, self._feeders = self._feeders, []
        for f in feeders:
            f.stop()
            f.appsrc.emit("end-of-stream")

    def status(self) -> dict[str, Any]:
        with self._lock:
            feeders = list(self._feeders)
        errors = [f.error for f in feeders if f.error]
        return {
            "state": "streaming" if feeders else "idle",
            "viewers": len(feeders),
            "frames": self._frames + sum(f.frames for f in feeders),
            "loops": max((f.loops for f in feeders), default=0),
            "error": errors[0] if errors else self.last_error,
        }


class Engine:
    def __init__(self, videos_dir: Path, host: str | None = None) -> None:
        self.videos_dir = videos_dir
        self.loop = GLib.MainLoop()
        self.server = GstRtspServer.RTSPServer()
        # One thread per client: a stream that is slow to start must not delay the others.
        self.server.get_thread_pool().set_max_threads(-1)
        self.config = ReplicatorConfig()
        self._host = host
        self._attached = 0
        self._mounts: dict[str, Mount] = {}
        self._problems: dict[str, str] = {}
        self.running = False

    # -- threads ------------------------------------------------------------------------------
    def call(self, func: Callable[[], Any], timeout: float = 30) -> Any:
        """Runs `func` on the main-loop thread and returns its result (from any other thread)."""
        done = threading.Event()
        box: dict[str, Any] = {}

        def run() -> bool:
            try:
                box["value"] = func()
            except Exception as exc:  # noqa: BLE001 - handed to the calling thread
                box["error"] = exc
            done.set()
            return False

        GLib.idle_add(run)
        if not done.wait(timeout):
            raise TimeoutError("replicator engine did not answer")
        if "error" in box:
            raise box["error"]
        return box["value"]

    # -- lifecycle ----------------------------------------------------------------------------
    def start(self, config: ReplicatorConfig) -> None:
        self.server.set_service(str(config.rtsp_port))
        self._attached = self.server.attach(None)
        if not self._attached:
            raise OSError(f"cannot listen on RTSP port {config.rtsp_port} (already in use?)")
        self.config = config
        self.running = True
        self._sync()

    def apply(self, config: ReplicatorConfig) -> None:
        """Switches to a new stream list; unchanged streams keep running."""
        if config.rtsp_port != self.config.rtsp_port:
            raise ValueError("rtsp_port cannot be changed while running: restart the replicator")
        self.config = config
        self._sync()

    def set_running(self, running: bool) -> None:
        self.running = running
        self._sync()

    def _sync(self) -> None:
        wanted = {s.name: s for s in self.config.streams if s.enabled} if self.running else {}
        for name in list(self._mounts):
            if wanted.get(name) != self._mounts[name].spec:
                self._mounts.pop(name).remove()
        self._problems = {}
        for name, spec in wanted.items():
            if name in self._mounts:
                continue
            path = self.videos_dir / spec.video
            if not path.is_file():
                self._problems[name] = f"video '{spec.video}' not found"
                continue
            info = probe(path)
            if info.get("error"):
                self._problems[name] = info["error"]
                continue
            self._mounts[name] = Mount(self.server, spec, path, info)

    def url(self, spec: StreamSpec) -> str:
        host = self.config.public_host or self._host or socket.gethostname()
        return f"rtsp://{host}:{self.config.rtsp_port}{spec.path}"

    def status(self) -> dict[str, Any]:
        streams = []
        for spec in self.config.streams:
            mount = self._mounts.get(spec.name)
            if mount:
                state = mount.status()
                state["video_info"] = mount.info
            else:
                problem = self._problems.get(spec.name)
                state = {
                    "state": "error" if problem else "stopped",
                    "viewers": 0,
                    "frames": 0,
                    "loops": 0,
                    "error": problem,
                    "video_info": None,
                }
            streams.append(
                {
                    "name": spec.name,
                    "video": spec.video,
                    "loop": spec.loop,
                    "start_offset_s": spec.start_offset_s,
                    "enabled": spec.enabled,
                    "url": self.url(spec),
                    **state,
                }
            )
        return {
            "running": self.running,
            "rtsp_port": self.config.rtsp_port,
            "streams": streams,
        }
