# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""rtvision-replicator: serve video files as looping, real-time RTSP streams.

    rtvision-replicator a.mp4 b.mp4 --streams 4        # cam-1 ... cam-4 from two videos
    rtvision-replicator --config replicator.yaml       # streams from a file
    rtvision-replicator --videos-dir /videos --api-port 8090   # controlled by the web UI
    rtvision-replicator --probe a.mp4                  # codec, size, fps, duration

Every stream is rtsp://<host>:8554/<name>. H.264/H.265 is served without re-encoding.
"""

from __future__ import annotations

import argparse
import signal
import sys
from pathlib import Path

from ..schemas import ConfigError
from .config import ReplicatorConfig, config_from_videos, load_config
from .library import LibraryError, VideoLibrary


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="rtvision-replicator",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("videos", nargs="*", type=Path, help="video files (H.264/H.265 mp4/mov/mkv/ts)")
    ap.add_argument("--config", type=Path, help="YAML configuration (saved to when using the API)")
    ap.add_argument("--videos-dir", type=Path, help="directory of the configured videos")
    ap.add_argument("--streams", type=int, help="number of streams (default: one per video)")
    ap.add_argument("--prefix", default="cam", help="stream name prefix (default: cam)")
    ap.add_argument("--port", type=int, help="RTSP port (default: 8554)")
    ap.add_argument("--no-loop", action="store_true", help="play each video once")
    ap.add_argument(
        "--stagger",
        type=float,
        default=5.0,
        metavar="S",
        help="start offset between streams that share a video (default: 5 s)",
    )
    ap.add_argument("--public-host", help="host name shown in the stream URLs")
    ap.add_argument("--api-port", type=int, help="also serve the HTTP control API on this port")
    ap.add_argument("--api-host", default="127.0.0.1", help="control API address")
    ap.add_argument("--probe", action="store_true", help="print video details and exit")
    return ap


def resolve(args: argparse.Namespace) -> tuple[ReplicatorConfig, Path]:
    """(configuration, videos directory) from the command line. Raises ConfigError."""
    if args.videos and args.config and args.config.exists():
        raise ConfigError("use either video files or --config, not both")
    if args.videos:
        dirs = {v.resolve().parent for v in args.videos}
        if len(dirs) > 1:
            raise ConfigError("all videos must be in the same directory (or use --config)")
        for v in args.videos:
            if not v.is_file():
                raise ConfigError(f"video '{v}' not found")
        cfg = config_from_videos(
            [v.name for v in args.videos],
            count=args.streams,
            prefix=args.prefix,
            rtsp_port=args.port or 8554,
            loop=not args.no_loop,
            stagger_s=args.stagger,
        )
        videos_dir = dirs.pop()
    elif args.config and args.config.exists():
        cfg = load_config(args.config)
        videos_dir = args.videos_dir or args.config.resolve().parent
    elif args.api_port and args.videos_dir:
        cfg = ReplicatorConfig()  # empty: configured later through the API
        videos_dir = args.videos_dir
    else:
        raise ConfigError("give video files, --config FILE, or --videos-dir DIR with --api-port")
    return (
        ReplicatorConfig(
            args.port or cfg.rtsp_port, args.public_host or cfg.public_host, cfg.streams
        ),
        videos_dir,
    )


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if not args.probe:
            cfg, videos_dir = resolve(args)
        elif not args.videos:
            raise ConfigError("--probe needs at least one video file")
    except (ConfigError, LibraryError) as exc:
        print(f"rtvision-replicator: {exc}", file=sys.stderr)
        return 2
    try:
        from . import api, engine
    except (ImportError, ValueError) as exc:
        print(
            f"rtvision-replicator: GStreamer RTSP server bindings not available ({exc}).\n"
            "Run tools/replicator/replicator.sh instead (uses the RTVision image), or install "
            "python3-gi and gir1.2-gst-rtsp-server-1.0.",
            file=sys.stderr,
        )
        return 2

    engine.init()
    if args.probe:
        for v in args.videos:
            print(f"{v}: {engine.probe(v)}")
        return 0

    eng = engine.Engine(videos_dir)
    try:
        eng.start(cfg)
    except OSError as exc:
        print(f"rtvision-replicator: {exc}", file=sys.stderr)
        return 1
    if args.api_port:
        library = VideoLibrary(videos_dir, probe=engine.probe)
        api.serve(api.Controller(eng, library, args.config), args.api_host, args.api_port)
        print(f"control API on http://{args.api_host}:{args.api_port}/api/status", flush=True)

    bad = 0
    for s in eng.status()["streams"]:
        if s["error"]:
            bad += 1
            print(f"  {s['name']:<16} ERROR {s['error']}", file=sys.stderr, flush=True)
        else:
            v = s["video_info"] or {}
            what = f"{v.get('codec')} {v.get('width')}x{v.get('height')} {v.get('fps')} fps"
            print(f"  {s['url']:<44} {s['video']} ({what})", flush=True)
    if bad and bad == len(cfg.streams) and not args.api_port:
        print("rtvision-replicator: no stream can be served", file=sys.stderr)
        return 1
    print("serving; Ctrl-C to stop", flush=True)

    for sig in (signal.SIGINT, signal.SIGTERM):
        engine.GLib.unix_signal_add(engine.GLib.PRIORITY_DEFAULT, sig, eng.loop.quit)
    eng.loop.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
