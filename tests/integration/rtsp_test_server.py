# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Minimal RTSP server for integration tests (GStreamer RTSP server via PyGObject).

Serves an H.264 MP4 (default: the DeepStream sample inside the container) at
rtsp://<host>:<port>/sample (once per client connection). RTP sessions send RTCP sender reports with
NTP time, so `timestamps.source: ntp` can be tested without a real camera. Run it inside the
Perceptra/DeepStream image, e.g.::

    docker run --rm --name rtsp-src --network perceptra-test \\
        -v $PWD/tests/integration:/it:ro --entrypoint python3 perceptra:local \\
        /it/rtsp_test_server.py
"""

from __future__ import annotations

import argparse

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")
from gi.repository import GLib, Gst, GstRtspServer  # noqa: E402

DEFAULT_FILE = "/opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--file", default=DEFAULT_FILE)
    ap.add_argument("--port", default="8554")
    ap.add_argument("--path", default="/sample")
    args = ap.parse_args()

    Gst.init(None)
    server = GstRtspServer.RTSPServer()
    server.set_service(args.port)
    factory = GstRtspServer.RTSPMediaFactory()
    # filesrc (qtdemux needs a seekable source); h264parse re-sends SPS/PPS every second.
    # Not shared: every client gets the file from the start.
    factory.set_launch(
        f"( filesrc location={args.file} ! qtdemux ! h264parse config-interval=1 "
        "! rtph264pay name=pay0 pt=96 config-interval=1 )"
    )
    server.get_mount_points().add_factory(args.path, factory)
    server.attach(None)
    print(f"serving rtsp://0.0.0.0:{args.port}{args.path}", flush=True)
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
