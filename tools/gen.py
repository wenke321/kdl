"""Generates the local test payload served by range_server.py."""

import os

here = os.path.dirname(os.path.abspath(__file__))
downloads = os.path.join(here, "..", "downloads")
os.makedirs(downloads, exist_ok=True)

path = os.path.join(downloads, "big.bin")
with open(path, "wb") as handle:
    handle.write(os.urandom(64 * 1024 * 1024))

print("generated %s (%d bytes)" % (path, os.path.getsize(path)))
