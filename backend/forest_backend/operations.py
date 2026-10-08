from contextlib import contextmanager
import fcntl
import os

from .common import BackendError
from .jsonfiles import private_directory


@contextmanager
def library_operation(paths):
    private_directory(paths.state)
    descriptor = os.open(paths.state / "operations.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise BackendError("Another library change or launch is in progress. Try again when it finishes.", "library_busy") from error
        yield
    finally:
        os.close(descriptor)
