from contextlib import contextmanager
import fcntl
import os

from .common import BackendError


@contextmanager
def library_operation(paths):
    paths.data.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(paths.data / "operations.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise BackendError("Another library change or launch is in progress. Try again when it finishes.", "library_busy") from error
        yield
    finally:
        os.close(descriptor)
