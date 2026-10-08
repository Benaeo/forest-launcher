"""Small, private JSON documents with atomic publication and explicit errors."""
import json
import os
from pathlib import Path
import stat
import tempfile

from .common import BackendError

MAX_DOCUMENT = 1024 * 1024


def private_directory(path):
    path.mkdir(parents=True, mode=0o700, exist_ok=True)
    if path.is_symlink() or not path.is_dir():
        raise BackendError(f"Expected a real directory: {path}")


def read_document(path):
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_DOCUMENT:
                raise BackendError(f"JSON file must be a regular file of at most 1 MiB: {path}")
            value = json.loads(source.read(MAX_DOCUMENT + 1))
        if not isinstance(value, dict):
            raise ValueError("expected an object")
        return value
    except FileNotFoundError:
        raise
    except (ValueError, UnicodeError, OSError) as error:
        raise BackendError(f"Could not read JSON file {path}: {error}", "invalid_json") from None


def write_document(path, value, *, exclusive=False):
    content = (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    write_bytes(path, content, exclusive=exclusive)


def write_bytes(path, content, *, exclusive=False):
    if len(content) > 16 * 1024 * 1024:
        raise BackendError(f"File exceeds the size limit: {path}")
    private_directory(path.parent)
    if path.is_symlink():
        raise BackendError(f"Refusing to replace a symbolic link: {path}")
    descriptor, temporary = tempfile.mkstemp(prefix=".forest-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as target:
            target.write(content)
            target.flush()
            os.fsync(target.fileno())
        os.chmod(temporary, 0o600)
        if exclusive:
            os.link(temporary, path)  # Publish fully written defaults only if absent.
        else:
            os.replace(temporary, path)
        directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        Path(temporary).unlink(missing_ok=True)
