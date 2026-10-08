"""Private artwork sessions; locks prevent cleanup of another live launcher."""
import fcntl
import os
from pathlib import Path
import shutil
import stat
import tempfile

from .common import BackendError

PREFIX = f"forest-launcher-artwork-{os.getuid()}-"
_fallback = None
_fallback_lock = None


def session_root():
    global _fallback, _fallback_lock
    supplied = os.environ.get("FOREST_ARTWORK_SESSION", "")
    if supplied:
        root = Path(supplied)
    else:
        if _fallback is None:
            _fallback = tempfile.TemporaryDirectory(prefix=PREFIX, dir="/tmp")
            _fallback_lock = os.open(Path(_fallback.name) / ".owner.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
            fcntl.flock(_fallback_lock, fcntl.LOCK_EX)
        root = Path(_fallback.name)
    try:
        info = root.lstat()
        if (root.parent != Path("/tmp") or not root.name.startswith(PREFIX)
                or not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o077):
            raise BackendError("Unsafe temporary artwork session directory.")
    except OSError:
        raise BackendError("The temporary artwork session is unavailable.") from None
    return root


def cleanup_stale():
    for root in Path("/tmp").glob(PREFIX + "*"):
        descriptor = None
        try:
            info = root.lstat()
            if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
                continue
            descriptor = os.open(root / ".owner.lock", os.O_RDWR | os.O_NOFOLLOW | os.O_NONBLOCK)
            lock_info = os.fstat(descriptor)
            if not stat.S_ISREG(lock_info.st_mode) or lock_info.st_uid != os.getuid():
                continue
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            shutil.rmtree(root)
        except (OSError, BackendError):
            # Never interrupt an active session or stop startup for cleanup.
            continue
        finally:
            if descriptor is not None:
                os.close(descriptor)
