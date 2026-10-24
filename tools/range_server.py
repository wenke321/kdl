"""Tiny HTTP server with HTTP Range support, used to exercise the downloader.

Serves downloads/big.bin, honouring `Range: bytes=start-end`, advertising an
ETag and throttling every connection so downloads can be interrupted.
"""

import http.server
import os
import re
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
FILE = os.path.join(ROOT, "..", "downloads", "big.bin")
RATE = 1024 * 1024  # bytes per second, per connection


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _throttle(self, nbytes):
        if RATE > 0:
            time.sleep(nbytes / float(RATE))

    def do_GET(self):
        try:
            size = os.path.getsize(FILE)
        except OSError:
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        start, end = 0, size - 1
        partial = False

        rng = self.headers.get("Range")
        if rng:
            match = re.match(r"bytes=(\d+)-(\d*)", rng.strip())
            if match:
                start = int(match.group(1))
                if match.group(2):
                    end = min(int(match.group(2)), size - 1)
                partial = True

        if start >= size:
            self.send_response(416)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        length = end - start + 1
        self.send_response(206 if partial else 200)
        self.send_header("Content-Length", str(length))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("ETag", '"fixed-etag-1"')
        self.send_header("Content-Type", "application/octet-stream")
        if partial:
            self.send_header("Content-Range", "bytes %d-%d/%d" % (start, end, size))
        self.end_headers()

        chunk = 64 * 1024
        with open(FILE, "rb") as handle:
            handle.seek(start)
            remaining = length
            while remaining > 0:
                data = handle.read(min(chunk, remaining))
                if not data:
                    break
                self.wfile.write(data)
                remaining -= len(data)
                self._throttle(len(data))

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8099
    print("serving %s on 127.0.0.1:%d" % (FILE, port), flush=True)
    Server(("127.0.0.1", port), Handler).serve_forever()
