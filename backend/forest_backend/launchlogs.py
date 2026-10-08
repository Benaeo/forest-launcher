"""Timestamped per-game logs and bounded, independently lived output capture."""
from datetime import datetime
import os
from pathlib import Path
import subprocess
import sys

from .common import game_name
from .jsonfiles import private_directory

LOG_COUNT = 5


def log_path(paths, game):
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S-%f")
    return paths.state / "logs" / game_name(game["title"]) / (timestamp + ".log")


def open_log(path):
    private_directory(path.parent)
    old = sorted((item for item in path.parent.glob("*.log") if item.is_file() and not item.is_symlink()),
                 key=lambda item: item.stat().st_mtime_ns, reverse=True)
    for previous in old[LOG_COUNT - 1:]:
        previous.unlink(missing_ok=True)
    # Existing capture helpers hold descriptors, not paths; pruning cannot
    # break a child's output pipe. They discard output once their file is gone.
    return os.open(path, os.O_RDWR | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)


def start_capture(descriptor, read_end):
    # Use a direct script path: no imports from the backend or user packages.
    return subprocess.Popen(
        [sys.executable, "-I", "-B", str(Path(__file__).with_name("logcapture.py")), str(descriptor)],
        stdin=read_end, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        pass_fds=(descriptor,), start_new_session=True,
    )
