# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""HTTP control API of the replicator (standard library only), used by the web UI.

GET    /api/status             streams with URL, state and video details
GET    /api/config             current configuration
PUT    /api/config             validate, apply and save a new configuration (JSON)
POST   /api/start, /api/stop   serve / unmount all streams
GET    /api/videos             videos with codec, resolution, fps and duration
PUT    /api/videos/<name>      upload (raw request body)
DELETE /api/videos/<name>      delete (refused while a stream uses it)

No authentication: bind it to localhost or a private network.
"""

from __future__ import annotations

import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import unquote, urlsplit

from ..schemas import ConfigError
from .config import ReplicatorConfig, parse_config, save_config
from .library import LibraryError, VideoLibrary

MAX_JSON = 256 * 1024


class Controller:
    """Request logic without HTTP. `engine` is an engine.Engine (or a test double)."""

    def __init__(self, engine: Any, library: VideoLibrary, config_path: Path | None) -> None:
        self.engine = engine
        self.library = library
        self.config_path = config_path
        self._lock = threading.Lock()  # one change at a time

    def status(self) -> dict[str, Any]:
        return self.engine.call(self.engine.status)

    def config(self) -> ReplicatorConfig:
        return self.engine.call(lambda: self.engine.config)

    def set_config(self, data: Any) -> dict[str, Any]:
        cfg = parse_config(data, "config")
        with self._lock:
            names = set(self.library.names())
            for i, s in enumerate(cfg.streams):
                if s.video not in names:
                    raise ConfigError(f"config: streams[{i}].video: '{s.video}' is not uploaded")
            self.engine.call(lambda: self.engine.apply(cfg))
            if self.config_path:
                save_config(cfg, self.config_path)
        return self.status()

    def set_running(self, running: bool) -> dict[str, Any]:
        with self._lock:
            self.engine.call(lambda: self.engine.set_running(running))
        return self.status()

    def delete_video(self, name: str) -> None:
        with self._lock:
            users = [s.name for s in self.config().streams if s.video == name]
            if users:
                raise LibraryError(f"'{name}' is used by {', '.join(users)}", 409)
            self.library.delete(name)


def make_handler(ctl: Controller) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "rtvision-replicator"
        sys_version = ""
        protocol_version = "HTTP/1.1"
        timeout = 60

        def log_message(self, fmt: str, *args: Any) -> None:  # quiet: status is polled
            pass

        def _send(self, status: int, body: Any) -> None:
            data = json.dumps(body).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            if status >= 400:
                # the request body may be unread: do not reuse the connection
                self.send_header("Connection", "close")
                self.close_connection = True
            self.end_headers()
            self.wfile.write(data)

        def _length(self) -> int:
            try:
                return int(self.headers.get("Content-Length") or 0)
            except ValueError:
                return 0

        def _json(self) -> Any:
            length = self._length()
            if not 0 < length <= MAX_JSON:
                raise LibraryError("a JSON body is required", 400)
            try:
                return json.loads(self.rfile.read(length))
            except ValueError as exc:
                raise LibraryError(f"invalid JSON: {exc}", 400) from exc

        def _route(self, method: str) -> None:
            path = unquote(urlsplit(self.path).path)
            video = path[len("/api/videos/") :] if path.startswith("/api/videos/") else None
            try:
                if method == "GET" and path == "/api/status":
                    self._send(200, ctl.status())
                elif method == "GET" and path == "/api/config":
                    self._send(200, ctl.config().to_dict())
                elif method == "PUT" and path == "/api/config":
                    self._send(200, ctl.set_config(self._json()))
                elif method == "POST" and path in ("/api/start", "/api/stop"):
                    self._send(200, ctl.set_running(path == "/api/start"))
                elif method == "GET" and path == "/api/videos":
                    self._send(200, {"videos": ctl.library.list()})
                elif method == "PUT" and video:
                    self._send(201, ctl.library.store(video, self.rfile, self._length()))
                elif method == "DELETE" and video:
                    ctl.delete_video(video)
                    self._send(200, {"deleted": video})
                else:
                    self._send(404, {"error": "not found"})
            except LibraryError as exc:
                self._send(exc.status, {"error": str(exc)})
            except (ConfigError, ValueError) as exc:
                self._send(400, {"error": str(exc)})
            except (BrokenPipeError, ConnectionResetError, TimeoutError):
                self.close_connection = True
            except Exception as exc:  # noqa: BLE001 - keep serving
                self._send(500, {"error": f"internal error: {exc}"})

        def do_GET(self) -> None:
            self._route("GET")

        def do_PUT(self) -> None:
            self._route("PUT")

        def do_POST(self) -> None:
            self._route("POST")

        def do_DELETE(self) -> None:
            self._route("DELETE")

    return Handler


def serve(ctl: Controller, host: str, port: int) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((host, port), make_handler(ctl))
    server.daemon_threads = True
    threading.Thread(target=server.serve_forever, name="replicator-api", daemon=True).start()
    return server
