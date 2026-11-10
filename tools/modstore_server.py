#!/usr/bin/env python3
"""A small web server for trying the mod store on this machine: it serves a folder (a built catalog, tools/modstore_catalog.py)
over plain HTTP on 127.0.0.1, which the game's request code (update_https_request) allows for a test and for nothing else.

    modstore_server.py FOLDER [--port 8765] [--no-ranges]

then in config.toml:

    [mods]
    store_url = "http://127.0.0.1:8765/catalog.json"
    store_key = "<the public key>"

It answers GET (with Range, as the game asks for the pieces of a file, unless --no-ranges) and nothing else, never follows a
path out of FOLDER, and can be told to misbehave for the tests: `hooks` (see Store) makes it drop a connection, serve other
bytes than the folder's, or answer another status. tools/test_modstore.py uses it as a module.
"""

import argparse
import http.server
import re
import threading
import time
import urllib.parse
from pathlib import Path


class Store:
    """a folder served; `requests` lists (path, range header) as they came; the hooks a test sets:

    - ranges: answer Range requests with 206 (False: ignore them, send the whole file with 200)
    - replace: {url path: bytes}, served in place of the folder's file (a hostile host serving other bytes)
    - status: {url path: status}, answered with no body
    - delay: seconds to wait before answering a request for a file under /mods/ (so that a test can stop a transfer in the middle)
    - fail_after: after so many requests to files under /mods/, close every connection with no answer (a lost connection)
    """

    def __init__(self, folder: Path, ranges: bool = True):
        self.folder = Path(folder).resolve()
        self.ranges = ranges
        self.replace = {}
        self.status = {}
        self.fail_after = None
        self.delay = 0
        self.requests = []
        self.lock = threading.Lock()
        self.server = None

    def start(self, port: int = 0) -> int:
        store = self

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *arguments):
                pass

            def do_GET(self):
                path = urllib.parse.unquote(self.path.split("?")[0])
                with store.lock:
                    store.requests.append((path, self.headers.get("Range")))
                    count = sum(1 for request, _ in store.requests if request.startswith("/mods/"))
                if store.fail_after is not None and path.startswith("/mods/") and count > store.fail_after:
                    self.close_connection = True
                    self.connection.close()
                    return
                if store.delay and path.startswith("/mods/"):
                    time.sleep(store.delay)
                if path in store.status:
                    self.send_response(store.status[path])
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                data = store.replace.get(path)
                if data is None:
                    target = (store.folder / path.lstrip("/")).resolve()
                    if store.folder not in target.parents or not target.is_file():
                        self.send_response(404)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                    data = target.read_bytes()
                match = re.fullmatch(r"bytes=(\d+)-(\d+)", self.headers.get("Range") or "")
                if match and store.ranges:
                    first, last = int(match.group(1)), min(int(match.group(2)), len(data) - 1)
                    if first >= len(data) or first > last:
                        self.send_response(416)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                        return
                    body = data[first:last + 1]
                    self.send_response(206)
                    self.send_header("Content-Range", f"bytes {first}-{last}/{len(data)}")
                else:
                    body = data
                    self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Accept-Ranges", "bytes" if store.ranges else "none")
                self.end_headers()
                self.wfile.write(body)

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        return self.server.server_address[1]

    def stop(self) -> None:
        if self.server:
            self.server.shutdown()
            self.server.server_close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--no-ranges", action="store_true")
    arguments = parser.parse_args()
    store = Store(arguments.folder, ranges=not arguments.no_ranges)
    port = store.start(arguments.port)
    print(f"serving {store.folder} at http://127.0.0.1:{port}/ (Ctrl-C to stop)")
    try:
        threading.Event().wait()
    except KeyboardInterrupt:
        store.stop()


if __name__ == "__main__":
    main()
