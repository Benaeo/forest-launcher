"""Detached, stdlib-only log sink; survives Forest closing while a game runs.

Invoked with an inherited private log descriptor and the child's output on stdin.
The file always stays at or below 10 MiB; overflow removes the oldest bytes.
"""
import os
import select
import sys

MAX_LOG_BYTES = 10 * 1024 * 1024


def capture(descriptor, source=0):
    with os.fdopen(descriptor, "r+b", buffering=0) as target:
        recording = True
        while True:
            if recording and os.fstat(target.fileno()).st_nlink == 0:
                target.truncate(0)
                recording = False
            if not select.select([source], [], [], 1)[0]:
                continue
            chunk = os.read(source, 64 * 1024)
            if not chunk:
                break
            if not recording:
                continue
            size = target.seek(0, os.SEEK_END)
            if size + len(chunk) <= MAX_LOG_BYTES:
                target.write(chunk)
                continue
            keep = max(0, MAX_LOG_BYTES - len(chunk))
            target.seek(max(0, size - keep))
            retained = target.read(keep) if keep else b""
            target.seek(0)
            target.write(retained + chunk[-MAX_LOG_BYTES:])
            target.truncate()


if __name__ == "__main__":
    capture(int(sys.argv[1]))
