"""Bounded private artwork storage; no third-party dependencies."""
import hashlib
import os
from pathlib import Path
import re
import stat
import tempfile
from .common import BackendError, expand_path

MAX_IMAGE = 16 * 1024 * 1024
KINDS = ("icon", "grid", "hero", "logo")

def image_extension(data):
    if data.startswith(b"\x89PNG\r\n\x1a\n"): return ".png"
    if data.startswith(b"\xff\xd8\xff"): return ".jpg"
    if data.startswith(b"RIFF") and data[8:12] == b"WEBP": return ".webp"
    if data.startswith(b"\0\0\x01\0"): return ".ico"
    raise BackendError("Choose a PNG, JPEG, WebP or ICO image.", "invalid_artwork")


def cache_bytes(paths, data):
    if not data or len(data) > MAX_IMAGE:
        raise BackendError("Artwork exceeds the 16 MiB limit.", "invalid_artwork")
    extension = image_extension(data)
    root = paths.data / "artwork"
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    if root.is_symlink():
        raise BackendError("Artwork directory must not be a symbolic link.")
    target = root / (hashlib.sha256(data).hexdigest() + extension)
    if target.exists():
        if target.is_symlink() or read_image(target) != data:
            raise BackendError("Cached artwork has changed unexpectedly.")
        return str(target)
    # A hard cap avoids turning image browsing into unbounded persistent storage.
    total, count = 0, 0
    with os.scandir(root) as entries:
        for entry in entries:
            count += 1
            if count > 20000:
                raise BackendError("Artwork cache is full.")
            if entry.is_file(follow_symlinks=False): total += entry.stat(follow_symlinks=False).st_size
    if total + len(data) > 512 * 1024 * 1024:
        raise BackendError("Artwork cache exceeds 512 MiB. Remove unused cached images before downloading more.")
    descriptor, temporary = tempfile.mkstemp(prefix=".forest-artwork-", dir=root)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return str(target)


def read_image(filename):
    descriptor = os.open(filename, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= MAX_IMAGE:
            raise BackendError("Artwork must be a regular image of at most 16 MiB.")
        data = stream.read(MAX_IMAGE + 1)
    if len(data) > MAX_IMAGE:
        raise BackendError("Artwork exceeds the size limit.")
    image_extension(data)
    return data


def managed_image(paths, value):
    if not isinstance(value, str) or len(value) > 8192 or "\0" in value:
        raise BackendError("Invalid artwork path.")
    if not value: return ""
    path = Path(value)
    root = paths.data / "artwork"
    if root.is_symlink() or path.parent != root or not re.fullmatch(r"[a-f0-9]{64}\.(png|jpg|webp|ico)", path.name):
        raise BackendError("Artwork must be imported into Forest before saving.")
    try:
        data = read_image(path)
        if path.name != hashlib.sha256(data).hexdigest() + image_extension(data):
            raise BackendError("Cached artwork does not match its identity.")
    except OSError:
        raise BackendError("Selected artwork is missing or unreadable.") from None
    return value


def validate_artwork(value, paths):
    if not isinstance(value, dict) or set(value) - {*KINDS, "extracted_icon"}:
        raise BackendError("Artwork contains unsupported fields.")
    return {key: managed_image(paths, item) for key, item in value.items()}


def import_image(paths, filename):
    if not isinstance(filename, str) or "\0" in filename or not filename or len(filename) > 8192:
        raise BackendError("Choose a local image.")
    try:
        return {"path": cache_bytes(paths, read_image(expand_path(filename)))}
    except OSError:
        raise BackendError("Could not read the selected image.") from None
