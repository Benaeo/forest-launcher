"""Bounded artwork storage and explicit, read-only SteamGridDB requests."""
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode, urlsplit
from urllib.request import Request, build_opener, HTTPRedirectHandler

from .common import BackendError, expand_path
from .shortcuts import atomic_write
from .artworktemp import session_root
from .jsonfiles import write_bytes
from contextlib import contextmanager

MAX_IMAGE = 16 * 1024 * 1024
MAX_JSON = 2 * 1024 * 1024
KINDS = ("icon", "grid", "hero", "logo")
ARTWORK_DIRECTORIES = {"icon": "icon", "grid": "grid", "hero": "banner", "logo": "logo", "extracted_icon": "extracted-icon"}
API = "https://www.steamgriddb.com/api/v2"


def api_key(value):
    if not isinstance(value, str) or len(value) > 256 or (value and not re.fullmatch(r"[A-Za-z0-9_-]+", value)):
        raise BackendError("SteamGridDB API key must be a token without spaces.")
    return value


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
    root = session_root()
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
        raise BackendError("Temporary artwork exceeds 512 MiB. Close Forest to clear this session.")
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
    temporary = path.parent == session_root() and bool(re.fullmatch(r"[a-f0-9]{64}\.(png|jpg|webp|ico)", path.name))
    saved = (path.parent.parent == root and path.parent.name in ARTWORK_DIRECTORIES.values()
             and bool(re.fullmatch(r"[\w-]+\.(png|jpg|webp|ico)", path.name)))
    if root.is_symlink() or path.parent.is_symlink() or not (temporary or saved):
        raise BackendError("Artwork must be imported into Forest before saving.")
    try:
        data = read_image(path)
        if temporary and path.name != hashlib.sha256(data).hexdigest() + image_extension(data):
            raise BackendError("Temporary artwork does not match its identity.")
    except OSError:
        raise BackendError("Selected artwork is missing or unreadable.") from None
    return value


def validate_artwork(value, paths):
    if not isinstance(value, dict) or set(value) - {*KINDS, "extracted_icon"}:
        raise BackendError("Artwork contains unsupported fields.")
    return {key: managed_image(paths, item) for key, item in value.items()}


@contextmanager
def persisted_artwork(paths, slug, selected):
    """Publish named assets on Save; restore old bytes if the game save fails."""
    result = dict.fromkeys((*KINDS, "extracted_icon"), "")
    payloads = {}
    for kind, source in selected.items():
        if not source:
            continue
        data = read_image(managed_image(paths, source))
        target = paths.data / "artwork" / ARTWORK_DIRECTORIES[kind] / (slug + image_extension(data))
        result[kind] = str(target)
        payloads[target] = data
    previous = {}
    for target in payloads:
        if target.is_symlink() or target.parent.is_symlink():
            raise BackendError("Saved artwork must not be a symbolic link.")
        previous[target] = read_image(target) if target.exists() else None
    published = []
    try:
        for target, data in payloads.items():
            write_bytes(target, data)
            published.append(target)
        yield result
    except BaseException:
        for target in reversed(published):
            if previous[target] is None:
                target.unlink(missing_ok=True)
            else:
                write_bytes(target, previous[target])
        raise


def remove_saved_artwork(paths, game, keep=()):
    """Only remove this game's named assets, never arbitrary JSON paths."""
    root = paths.data / "artwork"
    if root.is_symlink():
        raise BackendError("Artwork directory must not be a symbolic link.")
    for directory in ARTWORK_DIRECTORIES.values():
        parent = root / directory
        if parent.is_symlink():
            raise BackendError("Artwork directory must not be a symbolic link.")
        for extension in (".png", ".jpg", ".webp", ".ico"):
            path = parent / (game["slug"] + extension)
            if str(path) not in keep and not path.is_symlink():
                path.unlink(missing_ok=True)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def request_bytes(url, limit, key=""):
    headers = {"User-Agent": "Forest-Launcher", "Accept": "application/json" if key else "image/*"}
    if key: headers["Authorization"] = "Bearer " + key
    try:
        with build_opener(NoRedirect()).open(Request(url, headers=headers), timeout=15) as response:
            data = response.read(limit + 1)
            if len(data) > limit:
                raise BackendError("SteamGridDB response exceeds the size limit.")
            return data
    except HTTPError as error:
        code = error.code
        error.close()
        if code == 401: raise BackendError("SteamGridDB API key is missing or invalid.", "invalid_api_key") from None
        if code == 429: raise BackendError("SteamGridDB rate limit reached. Try again later.") from None
        raise BackendError(f"SteamGridDB request failed (HTTP {code}).") from None
    except (URLError, TimeoutError, OSError):
        raise BackendError("Could not reach SteamGridDB. Check your connection and try again.") from None


def api_json(endpoint, key):
    if not api_key(key):
        raise BackendError("Enter a SteamGridDB API key to browse artwork.", "missing_api_key")
    try:
        result = json.loads(request_bytes(API + endpoint, MAX_JSON, key))
        if not isinstance(result, dict) or result.get("success") is not True or not isinstance(result.get("data"), list):
            raise ValueError()
        return result["data"]
    except (ValueError, UnicodeError):
        raise BackendError("SteamGridDB returned an invalid response.") from None


def search_games(key, query, expanded=False):
    if not isinstance(query, str) or not 1 <= len(query.strip()) <= 256 or "\0" in query or type(expanded) is not bool:
        raise BackendError("Enter a game title of up to 256 characters.")
    # Match a typed title to one game, using SteamGridDB's autocomplete order.
    # Keep the legacy expanded argument valid, but never merge related titles.
    return {"games": title_suggestions(key, query)["games"][:1]}


def title_suggestions(key, query):
    if not isinstance(query, str) or not 1 <= len(query.strip()) <= 256 or "\0" in query:
        raise BackendError("Enter a game title of up to 256 characters.")
    games = []
    for game in api_json("/search/autocomplete/" + quote(query.strip(), safe=""), key):
        if (isinstance(game, dict) and type(game.get("id")) is int and 0 < game["id"] <= 2147483647
                and isinstance(game.get("name"), str) and 0 < len(game["name"]) <= 256):
            games.append({"id": game["id"], "name": game["name"]})
            if len(games) == 10:
                break
    return {"games": games}


def images(key, identity, kind, page=0):
    if type(identity) is not int or identity <= 0 or kind not in KINDS or type(page) is not int or not 0 <= page <= 1000:
        raise BackendError("Invalid artwork selection.")
    category = {"icon": "icons", "grid": "grids", "hero": "heroes", "logo": "logos"}[kind]
    filters = {"types": "static", "nsfw": "false", "epilepsy": "false", "limit": 30, "page": page}
    if kind == "grid":
        filters["dimensions"] = "600x900,342x482,660x930"
    query = urlencode(filters)
    values = api_json(f"/{category}/game/{identity}?{query}", key)[:30]
    result = []
    for value in values:
        if isinstance(value, dict) and all(type(value.get(field)) is str for field in ("url", "thumb")):
            if kind == "grid":
                width, height = value.get("width"), value.get("height")
                if type(width) is not int or type(height) is not int or not 0 < width < height:
                    continue
            if allowed_url(value["url"]) and allowed_url(value["thumb"]):
                result.append({"url": value["url"], "thumb": value["thumb"], "author": str(value.get("author", {}).get("name", ""))[:128] if isinstance(value.get("author"), dict) else ""})
    # Pagination follows the upstream page, not the number surviving validation.
    return {"images": result, "has_more": len(values) == 30 and page < 1000}


def allowed_url(url):
    if not isinstance(url, str) or len(url) > 8192: return False
    try:
        parts = urlsplit(url)
        return (parts.scheme == "https" and parts.hostname is not None and
                (parts.hostname == "steamgriddb.com" or parts.hostname.endswith(".steamgriddb.com"))
                and parts.port in (None, 443) and not parts.username and not parts.password)
    except ValueError:
        return False


def download_image(paths, url):
    if not allowed_url(url):
        raise BackendError("Artwork downloads must use HTTPS on SteamGridDB.")
    mapping = session_root() / "downloads" / (hashlib.sha256(url.encode()).hexdigest() + ".json")
    try:
        if mapping.parent.is_symlink(): raise BackendError("Unsafe artwork download cache.")
        descriptor = os.open(mapping, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > 4096: raise ValueError("Invalid artwork cache entry")
            cached = json.loads(stream.read(4097))
        if isinstance(cached, dict) and cached.get("url") == url and cached.get("path"):
            return {"path": managed_image(paths, cached.get("path"))}
    except (OSError, ValueError, BackendError):
        pass
    # API credentials are never sent to an image/CDN endpoint.
    path = cache_bytes(paths, request_bytes(url, MAX_IMAGE))
    if mapping.parent.is_symlink():
        raise BackendError("Artwork download cache must not be a symbolic link.")
    atomic_write(mapping, json.dumps({"url": url, "path": path}))
    return {"path": path}


def import_image(paths, filename):
    if not isinstance(filename, str) or "\0" in filename or not filename or len(filename) > 8192:
        raise BackendError("Choose a local image.")
    try:
        return {"path": cache_bytes(paths, read_image(expand_path(filename)))}
    except OSError:
        raise BackendError("Could not read the selected image.") from None
