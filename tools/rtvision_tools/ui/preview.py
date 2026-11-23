# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Annotated tiled video (RTSP from RTVision) -> JPEG frames for the browser.

One pipeline for all viewers; it runs only while someone is watching and reconnects on errors.
"""

from __future__ import annotations

import threading
import time
from collections.abc import Iterator
from typing import Any

RETRY_S = 1.0
STALL_S = 8.0  # no frame for this long: rebuild the pipeline
LINGER_S = 5.0  # keep running after the last viewer left (page changes)


def pipeline_description(url: str, fps: int, quality: int) -> str:
    if any(c in url for c in " \"'!\n\r\t"):
        raise ValueError("invalid preview URL")
    return (
        f"rtspsrc location={url} protocols=tcp latency=100 ! rtph264depay ! h264parse "
        "! nvv4l2decoder ! nvvideoconvert ! video/x-raw,format=I420 "
        f"! videorate drop-only=true ! video/x-raw,framerate={fps}/1 "
        f"! jpegenc quality={quality} "
        "! appsink name=out max-buffers=1 drop=true sync=false emit-signals=true"
    )


class Preview:
    def __init__(self, url: str, fps: int = 15, quality: int = 80) -> None:
        self._description = pipeline_description(url, fps, quality)
        self._cond = threading.Condition()
        self._frame: bytes | None = None
        self._seq = 0
        self._viewers = 0
        self._left_at = 0.0
        self._thread: threading.Thread | None = None
        self.error: str | None = None

    def warm_up(self) -> None:
        """Loads GStreamer in the background: its first start scans all plugins (seconds)."""

        def load() -> None:
            try:
                import gi

                gi.require_version("Gst", "1.0")
                from gi.repository import Gst

                Gst.init(None)
                Gst.parse_launch(self._description).set_state(Gst.State.NULL)
            except Exception as exc:  # noqa: BLE001 - reported when someone opens the page
                self.error = str(exc)

        threading.Thread(target=load, name="ui-preview-warm-up", daemon=True).start()

    def frames(self, timeout: float = 20.0) -> Iterator[bytes]:
        """Yields new JPEG frames until no frame arrives for `timeout` seconds."""
        with self._cond:
            self._viewers += 1
            if self._thread is None:
                self._thread = threading.Thread(target=self._run, name="ui-preview", daemon=True)
                self._thread.start()
            seq = self._seq
        try:
            while True:
                with self._cond:
                    if not self._cond.wait_for(lambda seq=seq: self._seq != seq, timeout):
                        return
                    seq, frame = self._seq, self._frame
                if frame:
                    yield frame
        finally:
            with self._cond:
                self._viewers -= 1
                self._left_at = time.monotonic()

    def _wanted(self) -> bool:
        return self._viewers > 0 or time.monotonic() - self._left_at < LINGER_S

    def _run(self) -> None:
        while True:
            try:
                self._play()
            except Exception as exc:  # noqa: BLE001 - shown in the UI, retried
                self.error = str(exc)
            with self._cond:
                if not self._wanted():
                    self._thread = None
                    return
            time.sleep(RETRY_S)

    def _play(self) -> None:
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import Gst

        Gst.init(None)
        pipeline = Gst.parse_launch(self._description)
        last = [time.monotonic()]

        def on_sample(sink: Any) -> Any:
            buf = sink.emit("pull-sample").get_buffer()
            data = buf.extract_dup(0, buf.get_size())
            last[0] = time.monotonic()
            with self._cond:
                self._frame = data
                self._seq += 1
                self._cond.notify_all()
            self.error = None
            return Gst.FlowReturn.OK

        pipeline.get_by_name("out").connect("new-sample", on_sample)
        bus = pipeline.get_bus()
        pipeline.set_state(Gst.State.PLAYING)
        try:
            while self._wanted():
                msg = bus.timed_pop_filtered(
                    250 * Gst.MSECOND, Gst.MessageType.ERROR | Gst.MessageType.EOS
                )
                if msg is not None:
                    if msg.type == Gst.MessageType.ERROR:
                        raise RuntimeError(msg.parse_error()[0].message)
                    raise RuntimeError("the annotated video stream ended")
                if time.monotonic() - last[0] > STALL_S:
                    raise RuntimeError("no video: waiting for a camera")
        finally:
            pipeline.set_state(Gst.State.NULL)
