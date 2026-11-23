# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""rtvision-ui: web UI server (standard library only).

    rtvision-ui --port 8080 --ops-url http://rtvision:9464 --rest-url http://rtvision:9000 \\
        --preview-url rtsp://rtvision:8554/ds-test --replicator-url http://replicator:8090

There is no login: bind it to localhost or a trusted network. Requests that change something need
the X-RTVision header (so other web sites cannot send them through your browser), and the Host
header must be an IP address, localhost or listed in --allowed-host (DNS rebinding).
"""

from __future__ import annotations

import argparse
import http.client
import ipaddress
import json
import os
import re
import signal
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlsplit

from .. import __version__
from .backend import BackendError, CameraUrls, RTVisionClient, Sampler
from .hardware import HardwareMonitor
from .preview import Preview

STATIC = Path(__file__).parent / "static"
TYPES = {
    ".html": "text/html",
    ".js": "text/javascript",
    ".css": "text/css",
    ".svg": "image/svg+xml",
}
PAGES = ("/", "/live", "/performance", "/replicator")
MAX_JSON = 256 * 1024
CHUNK = 1024 * 1024
BOUNDARY = "rtvisionframe"
CSP = (
    "default-src 'self'; img-src 'self' blob: data:; style-src 'self'; script-src 'self'; "
    "frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
)
_VIDEO_PATH = re.compile(r"^/api/videos/[^/]+$")
_REPLICATOR_ROUTES = {
    ("GET", "/api/status"),
    ("GET", "/api/config"),
    ("PUT", "/api/config"),
    ("POST", "/api/start"),
    ("POST", "/api/stop"),
    ("GET", "/api/videos"),
}


def host_allowed(header: str | None, extra: frozenset[str]) -> bool:
    if not header:
        return False
    name = header.strip().lower()
    if name.startswith("["):
        name = name[1:].partition("]")[0]
    elif name.count(":") == 1:
        name = name.partition(":")[0]
    if name == "localhost" or name in extra:
        return True
    try:
        ipaddress.ip_address(name)
        return True
    except ValueError:
        return False


def replicator_route_allowed(method: str, path: str) -> bool:
    if (method, path) in _REPLICATOR_ROUTES:
        return True
    return method in ("PUT", "DELETE") and bool(_VIDEO_PATH.match(path))


class App:
    def __init__(
        self,
        sampler: Sampler,
        client: RTVisionClient,
        preview: Preview | None,
        replicator_url: str | None,
        allowed_hosts: frozenset[str] = frozenset(),
    ) -> None:
        self.sampler = sampler
        self.client = client
        self.preview = preview
        self.replicator_url = replicator_url
        self.allowed_hosts = allowed_hosts


def make_handler(app: App) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "rtvision-ui"
        sys_version = ""
        protocol_version = "HTTP/1.1"
        timeout = 120

        def log_message(self, fmt: str, *args: Any) -> None:  # the page polls: stay quiet
            pass

        def _headers(self, status: int, ctype: str, length: int | None, cache: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", ctype)
            if length is not None:
                self.send_header("Content-Length", str(length))
            self.send_header("Cache-Control", cache)
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", CSP)
            if status >= 400 or length is None:
                self.send_header("Connection", "close")
                self.close_connection = True
            self.end_headers()

        def _json(self, status: int, body: Any) -> None:
            data = json.dumps(body).encode()
            self._headers(status, "application/json", len(data), "no-store")
            self.wfile.write(data)

        def _body(self) -> Any:
            try:
                length = int(self.headers.get("Content-Length") or 0)
            except ValueError:
                length = 0
            if not 0 < length <= MAX_JSON:
                raise BackendError("a JSON body is required", 400)
            try:
                return json.loads(self.rfile.read(length))
            except ValueError as exc:
                raise BackendError("invalid JSON", 400) from exc

        def _static(self, path: str) -> None:
            name = "index.html" if path in PAGES else path.removeprefix("/static/")
            file = STATIC / name
            if "/" in name or file.suffix not in TYPES or not file.is_file():
                self._json(404, {"error": "not found"})
                return
            data = file.read_bytes()
            self._headers(200, f"{TYPES[file.suffix]}; charset=utf-8", len(data), "no-cache")
            self.wfile.write(data)

        def _mjpeg(self) -> None:
            if app.preview is None:
                self._json(503, {"error": "preview is not configured"})
                return
            frames = app.preview.frames()
            try:
                first = next(frames, None)
                if first is None:
                    self._json(503, {"error": app.preview.error or "no video yet"})
                    return
                self._headers(
                    200, f"multipart/x-mixed-replace; boundary={BOUNDARY}", None, "no-store"
                )
                frame: bytes | None = first
                while frame is not None:
                    self.wfile.write(
                        f"--{BOUNDARY}\r\nContent-Type: image/jpeg\r\n"
                        f"Content-Length: {len(frame)}\r\n\r\n".encode()
                        + frame
                        + b"\r\n"
                    )
                    frame = next(frames, None)
            finally:
                frames.close()  # type: ignore[attr-defined]

        def _proxy_replicator(self, method: str, path: str) -> None:
            """Streams the request to the replicator (uploads can be gigabytes) and back."""
            if not app.replicator_url:
                raise BackendError("the replicator is not configured", 503)
            if not replicator_route_allowed(method, unquote(path)):
                self._json(404, {"error": "not found"})
                return
            target = urlsplit(app.replicator_url)
            conn = http.client.HTTPConnection(
                target.hostname or "", target.port or 80, timeout=self.timeout
            )
            try:
                length = int(self.headers.get("Content-Length") or 0)
                conn.putrequest(method, path)
                conn.putheader("Content-Length", str(length))
                conn.endheaders()
                left = length
                try:
                    while left > 0:
                        chunk = self.rfile.read(min(CHUNK, left))
                        if not chunk:
                            raise ConnectionResetError
                        conn.send(chunk)
                        left -= len(chunk)
                except BrokenPipeError:
                    pass  # refused early (e.g. file too large): its answer follows
                resp = conn.getresponse()
                data = resp.read()
                self._headers(resp.status, "application/json", len(data), "no-store")
                self.wfile.write(data)
            except (http.client.HTTPException, ConnectionRefusedError, OSError) as exc:
                if isinstance(exc, (ConnectionResetError, TimeoutError)):
                    self.close_connection = True
                    raise
                raise BackendError("the replicator is not reachable", 502) from exc
            finally:
                conn.close()

        def _route(self, method: str) -> None:
            path = urlsplit(self.path).path
            try:
                if not host_allowed(self.headers.get("Host"), app.allowed_hosts):
                    self._json(403, {"error": "host not allowed (see --allowed-host)"})
                elif method != "GET" and self.headers.get("X-RTVision") != "1":
                    self._json(403, {"error": "missing X-RTVision header"})
                elif path.startswith("/api/replicator/"):
                    self._proxy_replicator(method, path.replace("/api/replicator", "/api", 1))
                elif method == "GET":
                    self._get(path)
                elif method == "POST" and path == "/api/streams":
                    self._json(201, app.client.add_stream(self._body()))
                elif method == "DELETE" and path.startswith("/api/streams/"):
                    camera = unquote(path[len("/api/streams/") :])
                    url = None
                    if self.headers.get("Content-Length", "0") != "0":
                        body = self._body()
                        url = str(body.get("url") or "") if isinstance(body, dict) else None
                    self._json(200, app.client.remove_stream(camera, url))
                else:
                    self._json(404, {"error": "not found"})
            except BackendError as exc:
                self._json(exc.status, {"error": str(exc)})
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                self.close_connection = True
            except Exception as exc:  # noqa: BLE001 - keep serving
                print(f"rtvision-ui: {method} {path}: {exc!r}", file=sys.stderr, flush=True)
                self.close_connection = True

        def _get(self, path: str) -> None:
            if path == "/api/overview":
                self._json(
                    200,
                    {
                        **app.sampler.overview(),
                        "ui_version": __version__,
                        "preview": app.preview is not None,
                        "preview_error": app.preview.error if app.preview else None,
                        "replicator": app.replicator_url is not None,
                    },
                )
            elif path == "/api/hardware":
                self._json(200, app.sampler.hardware())
            elif path == "/api/history":
                self._json(200, app.sampler.history())
            elif path == "/preview.mjpg":
                self._mjpeg()
            elif path == "/healthz":
                self._json(200, {"status": "ok"})
            elif path.startswith("/api/"):
                self._json(404, {"error": "not found"})
            else:
                self._static(path)

        def do_GET(self) -> None:
            self._route("GET")

        def do_PUT(self) -> None:
            self._route("PUT")

        def do_POST(self) -> None:
            self._route("POST")

        def do_DELETE(self) -> None:
            self._route("DELETE")

    return Handler


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request: Any, client_address: Any) -> None:
        if not isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):
            super().handle_error(request, client_address)


def main(argv: list[str] | None = None) -> int:
    env = os.environ.get
    ap = argparse.ArgumentParser(
        prog="rtvision-ui",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("--host", default=env("RTVISION_UI_HOST", "127.0.0.1"))
    ap.add_argument("--port", type=int, default=int(env("RTVISION_UI_PORT", "8080")))
    ap.add_argument("--ops-url", default=env("RTVISION_UI_OPS_URL", "http://127.0.0.1:9464"))
    ap.add_argument("--rest-url", default=env("RTVISION_UI_REST_URL", "http://127.0.0.1:9000"))
    ap.add_argument("--preview-url", default=env("RTVISION_UI_PREVIEW_URL", ""))
    ap.add_argument("--preview-fps", type=int, default=int(env("RTVISION_UI_PREVIEW_FPS", "15")))
    ap.add_argument("--replicator-url", default=env("RTVISION_UI_REPLICATOR_URL", ""))
    ap.add_argument(
        "--state-file",
        default=env("RTVISION_UI_STATE_FILE", ""),
        help="private file that remembers the URLs of cameras added here (needed to remove them)",
    )
    ap.add_argument(
        "--allowed-host",
        action="append",
        default=[h for h in env("RTVISION_UI_ALLOWED_HOSTS", "").split(",") if h],
        help="host name (besides localhost and IP addresses) the UI may be opened with",
    )
    args = ap.parse_args(argv)

    urls = CameraUrls(Path(args.state_file) if args.state_file else None)
    client = RTVisionClient(args.ops_url, args.rest_url, urls=urls)
    sampler = Sampler(client, HardwareMonitor())
    preview = Preview(args.preview_url, args.preview_fps) if args.preview_url else None
    app = App(
        sampler,
        client,
        preview,
        args.replicator_url or None,
        frozenset(h.strip().lower() for h in args.allowed_host),
    )
    try:
        server = Server((args.host, args.port), make_handler(app))
    except OSError as exc:
        print(f"rtvision-ui: cannot listen on {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1
    sampler.start()
    if preview:
        preview.warm_up()
    print(f"RTVision UI on http://{args.host}:{args.port}/", flush=True)
    # As PID 1 in a container, Python ignores SIGTERM unless it is handled.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
