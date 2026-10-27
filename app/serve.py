#!/usr/bin/env python3
"""Serve the OncoSense Monitor on localhost and open it in the browser.

    python3 app/serve.py            # http://localhost:8765
    python3 app/serve.py --port 9000 --no-open

Web Serial (USB access) only works on secure pages, and localhost counts as
secure, so this is all the "server" the monitor needs. Use Chrome or Edge.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import pathlib
import webbrowser

APP_DIR = pathlib.Path(__file__).resolve().parent


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript",
        ".mjs": "text/javascript",
        ".webmanifest": "application/manifest+json",
        ".svg": "image/svg+xml",
    }

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def log_message(self, fmt: str, *args) -> None:  # keep the terminal quiet
        pass


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-open", action="store_true")
    args = ap.parse_args()

    handler = functools.partial(Handler, directory=str(APP_DIR))
    with http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler) as httpd:
        url = f"http://localhost:{args.port}/"
        print(f"OncoSense Monitor running at {url}  (Ctrl-C to stop)")
        print("Open it in Chrome or Edge to connect to the band over USB.")
        if not args.no_open:
            webbrowser.open(url)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
