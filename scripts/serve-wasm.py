#!/usr/bin/env python3
"""Serve the experimental WASM smoke locally, with the headers its pthread ABI needs."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class IsolatedHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", default="build/wasm-smoke")
    parser.add_argument("--port", type=int, default=8000)
    arguments = parser.parse_args()
    directory = Path(arguments.directory).resolve(strict=True)
    if not (directory / "index.html").is_file():
        parser.error("build the wasm-smoke preset first (index.html is missing)")
    handler = partial(IsolatedHandler, directory=str(directory))
    with ThreadingHTTPServer(("127.0.0.1", arguments.port), handler) as server:
        print(f"Serving WASM smoke on loopback port {server.server_port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
