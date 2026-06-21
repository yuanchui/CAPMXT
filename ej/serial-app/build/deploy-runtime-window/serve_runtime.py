#!/usr/bin/env python3
"""Serial Terminal 远程有效期 JSON 静态服务（替代 Nginx）。"""
from __future__ import annotations

import os
import sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

PORT = int(os.environ.get("HTTP_PORT", "19876"))
WEB_ROOT = os.environ.get("WEB_ROOT", "/var/www/mxt-runtime")


class RuntimeWindowHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_ROOT, **kwargs)

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        if self.path.endswith(".json"):
            self.send_header("Content-Type", "application/json; charset=utf-8")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write(f"[mxt-runtime] {self.address_string()} - {fmt % args}\n")


def main() -> None:
    if not os.path.isdir(WEB_ROOT):
        print(f"WEB_ROOT 不存在: {WEB_ROOT}", file=sys.stderr)
        sys.exit(1)
    os.chdir(WEB_ROOT)
    server = ThreadingHTTPServer(("0.0.0.0", PORT), RuntimeWindowHandler)
    print(f"Serving {WEB_ROOT} on 0.0.0.0:{PORT}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("stopped", flush=True)


if __name__ == "__main__":
    main()
