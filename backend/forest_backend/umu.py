"""Manage UMU's official zipapp without executing downloaded code during setup."""

from contextlib import contextmanager
import fcntl
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tarfile
import tempfile
import time
import urllib.request
from uuid import uuid4
import zipfile
import zlib

from .common import BackendError, Paths, xdg_home


REPOSITORY = "Open-Wine-Components/umu-launcher"
RELEASE_API = f"https://api.github.com/repos/{REPOSITORY}/releases/latest"
DOWNLOAD_PREFIX = f"https://github.com/{REPOSITORY}/releases/download/"
CHECK_INTERVAL = 24 * 60 * 60
RETRY_INTERVAL = 15 * 60
MAX_METADATA = 1024 * 1024
MAX_DOWNLOAD = 16 * 1024 * 1024
MAX_UNPACKED_ZIP = 64 * 1024 * 1024
TAG_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.+-]{0,79}\Z")
SHA_PATTERN = re.compile(r"[0-9a-f]{64}\Z")


def fetch_bytes(url: str, limit: int, timeout=20) -> bytes:
    request = urllib.request.Request(url, headers={
        "User-Agent": "Forest-Launcher/0.1", "Accept": "application/vnd.github+json"
        if url == RELEASE_API else "application/octet-stream",
    })
    with urllib.request.urlopen(request, timeout=timeout) as response:
        if not response.geturl().startswith("https://"):
            raise BackendError("UMU download was redirected to an insecure URL.", "umu_download")
        content = response.read(limit + 1)
    if len(content) > limit:
        raise BackendError("UMU download exceeded its size limit.", "umu_download")
    return content


def release_asset(document: dict) -> dict:
    if not isinstance(document, dict) or document.get("draft") or document.get("prerelease"):
        raise BackendError("UMU release metadata is invalid.", "umu_download")
    version = document.get("tag_name", "")
    assets = document.get("assets", [])
    if not isinstance(version, str) or not TAG_PATTERN.fullmatch(version) or not isinstance(assets, list):
        raise BackendError("UMU release metadata is invalid.", "umu_download")
    matches = [asset for asset in assets if isinstance(asset, dict)
               and isinstance(asset.get("name"), str)
               and asset["name"].startswith("umu-launcher-") and asset["name"].endswith("-zipapp.tar")]
    if len(matches) != 1:
        raise BackendError("The UMU release has no unambiguous self-contained launcher.", "umu_download")
    asset = matches[0]
    url, digest, size = asset.get("browser_download_url"), asset.get("digest"), asset.get("size")
    if not isinstance(url, str) or not url.startswith(DOWNLOAD_PREFIX):
        raise BackendError("UMU release points to an untrusted download URL.", "umu_download")
    if not isinstance(digest, str) or not digest.startswith("sha256:") or not SHA_PATTERN.fullmatch(digest[7:]):
        raise BackendError("UMU release is missing a valid SHA-256 checksum.", "umu_download")
    if type(size) is not int or not 0 < size <= MAX_DOWNLOAD:
        raise BackendError("UMU release has an invalid download size.", "umu_download")
    return {"version": version, "url": url, "sha256": digest[7:], "size": size}


def validate_zipapp(content: bytes):
    if not content.startswith(b"#!/usr/bin/env python3\n"):
        raise BackendError("UMU launcher has an invalid executable header.", "umu_download")
    try:
        with zipfile.ZipFile(io.BytesIO(content)) as archive:
            if sum(entry.file_size for entry in archive.infolist()) > MAX_UNPACKED_ZIP:
                raise BackendError("UMU launcher exceeds its unpacked size limit.", "umu_download")
            names = archive.namelist()
            if len(names) != len(set(names)):
                raise BackendError("UMU launcher has duplicate archive entries.", "umu_download")
            if "__main__.py" not in names or "umu/__init__.py" not in names:
                raise BackendError("UMU launcher is missing its entry point or module.", "umu_download")
            if archive.testzip() is not None:
                raise BackendError("UMU launcher failed its integrity check.", "umu_download")
    except (zipfile.BadZipFile, RuntimeError, OSError, EOFError, zlib.error) as exc:
        raise BackendError(f"UMU launcher archive is invalid: {exc}", "umu_download") from None


def unpack_launcher(content: bytes) -> bytes:
    try:
        with tarfile.open(fileobj=io.BytesIO(content), mode="r:") as archive:
            matches = [member for member in archive.getmembers() if PurePosixPath(member.name).name == "umu-run"]
            if len(matches) != 1:
                raise BackendError("UMU download has no unambiguous launcher file.", "umu_download")
            member = matches[0]
            name = PurePosixPath(member.name)
            if name.is_absolute() or ".." in name.parts or not member.isfile() or not 0 < member.size <= MAX_DOWNLOAD:
                raise BackendError("UMU download contains an unsafe launcher entry.", "umu_download")
            stream = archive.extractfile(member)
            if stream is None:
                raise BackendError("Cannot read UMU launcher from its download.", "umu_download")
            launcher = stream.read(MAX_DOWNLOAD + 1)
            if len(launcher) != member.size:
                raise BackendError("UMU launcher download is incomplete.", "umu_download")
    except (tarfile.TarError, OSError) as exc:
        raise BackendError(f"UMU download archive is invalid: {exc}", "umu_download") from None
    validate_zipapp(launcher)
    return launcher


class UMUManager:
    def __init__(self, paths: Paths):
        self.directory = paths.data / "components/umu"
        self.metadata_file = self.directory / "release.json"

    def metadata(self) -> dict:
        try:
            with self.metadata_file.open("rb") as stream:
                raw = stream.read(MAX_METADATA + 1)
            result = json.loads(raw) if len(raw) <= MAX_METADATA else {}
            return result if isinstance(result, dict) else {}
        except (OSError, ValueError):
            return {}

    def managed_path(self, metadata: dict) -> Path | None:
        version, digest, generation = metadata.get("version"), metadata.get("asset_sha256"), metadata.get("generation")
        if (not isinstance(version, str) or not TAG_PATTERN.fullmatch(version)
                or not isinstance(digest, str) or not SHA_PATTERN.fullmatch(digest)
                or not isinstance(generation, str) or not re.fullmatch(r"[0-9a-f]{12}", generation)):
            return None
        return self.directory / "versions" / f"{version}-{digest[:16]}-{generation}" / "umu-run"

    def status(self) -> dict:
        metadata = self.metadata()
        managed = self.managed_path(metadata)
        if managed and managed.is_file() and os.access(managed, os.X_OK):
            try:
                with managed.open("rb") as stream:
                    content = stream.read(MAX_DOWNLOAD + 1)
                valid = 0 < len(content) <= MAX_DOWNLOAD and hashlib.sha256(content).hexdigest() == metadata.get("binary_sha256")
            except OSError:
                valid = False
            if valid:
                return self._status(str(managed), "managed", metadata)
        system = shutil.which("umu-run")
        if system:
            return self._status(system, "system", metadata)
        legacy = xdg_home("XDG_DATA_HOME", Path.home() / ".local/share") / "faugus-launcher/umu-run"
        if legacy.is_file() and os.access(legacy, os.X_OK):
            return self._status(str(legacy), "faugus", metadata)
        return self._status("", "missing", metadata)

    @staticmethod
    def _status(path: str, source: str, metadata: dict) -> dict:
        return {"available": bool(path), "path": path, "source": source,
                "version": metadata.get("version", "") if source == "managed" else "",
                "last_error": metadata.get("last_error", ""),
                "message": "UMU is managed automatically." if path else "UMU will be downloaded automatically."}

    def write_metadata(self, metadata: dict):
        self.directory.mkdir(parents=True, mode=0o700, exist_ok=True)
        descriptor, name = tempfile.mkstemp(prefix=".release-", dir=self.directory)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(metadata, stream, ensure_ascii=False)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(name, self.metadata_file)
        finally:
            Path(name).unlink(missing_ok=True)

    @contextmanager
    def update_lock(self):
        self.directory.mkdir(parents=True, mode=0o700, exist_ok=True)
        descriptor = os.open(self.directory / "update.lock", os.O_CREAT | os.O_RDWR, 0o600)
        try:
            deadline = time.monotonic() + 45
            while True:
                try:
                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.monotonic() >= deadline:
                        raise BackendError("Another UMU update is still in progress. Please try again shortly.", "umu_busy")
                    time.sleep(0.1)
            yield
        finally:
            os.close(descriptor)

    def clean_staging(self):
        for path in self.directory.glob(".release-*"):
            if path.is_file() or path.is_symlink():
                path.unlink()
        for path in (self.directory / "versions").glob(".install-*"):
            if path.is_symlink():
                path.unlink()
            elif path.is_dir():
                shutil.rmtree(path)

    def install(self, launcher: bytes, asset: dict, metadata: dict) -> dict:
        generation = uuid4().hex[:12]
        target = self.managed_path({"version": asset["version"], "asset_sha256": asset["sha256"], "generation": generation})
        assert target is not None
        target.parent.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        # Never overwrite a zipapp that an already-running game may still import from.
        with tempfile.TemporaryDirectory(prefix=".install-", dir=target.parent.parent) as staging:
            staged = Path(staging) / "umu-run"
            with staged.open("wb") as stream:
                stream.write(launcher)
                stream.flush()
                os.fsync(stream.fileno())
            staged.chmod(0o755)
            os.rename(staging, target.parent)
        result = {**metadata, "version": asset["version"], "asset_sha256": asset["sha256"], "generation": generation,
                  "binary_sha256": hashlib.sha256(launcher).hexdigest(), "repository": REPOSITORY,
                  "last_check": time.time(), "last_attempt": time.time(), "last_error": ""}
        self.write_metadata(result)
        return self.status()

    def prepare(self, *, required=False, force=False) -> dict:
        try:
            with self.update_lock():
                self.clean_staging()
                metadata = self.metadata()
                status = self.status()
                now = time.time()
                last_check, last_attempt = metadata.get("last_check", 0), metadata.get("last_attempt", 0)
                last_check = last_check if type(last_check) in (int, float) else 0
                last_attempt = last_attempt if type(last_attempt) in (int, float) else 0
                if not force:
                    if status["source"] == "managed" and last_check > 0 and 0 <= now - last_check < CHECK_INTERVAL:
                        return status
                    if (status["available"] or not required) and last_attempt > 0 and 0 <= now - last_attempt < RETRY_INTERVAL:
                        return status
                metadata["last_attempt"] = now
                self.write_metadata(metadata)
                try:
                    asset = release_asset(json.loads(fetch_bytes(RELEASE_API, MAX_METADATA)))
                    if (status["source"] == "managed" and metadata.get("repository") == REPOSITORY
                            and metadata.get("version") == asset["version"]
                            and metadata.get("asset_sha256") == asset["sha256"]):
                        self.write_metadata({**metadata, "last_check": time.time(), "last_error": ""})
                        return self.status()
                    payload = fetch_bytes(asset["url"], asset["size"])
                    if len(payload) != asset["size"] or hashlib.sha256(payload).hexdigest() != asset["sha256"]:
                        raise BackendError("UMU download did not match its size or SHA-256 checksum.", "umu_download")
                    return self.install(unpack_launcher(payload), asset, metadata)
                except (BackendError, OSError, ValueError) as exc:
                    self.write_metadata({**metadata, "last_error": str(exc)})
                    status = self.status()
                    status["message"] = f"UMU update failed: {exc}"
                    return status
        except (BackendError, OSError) as exc:
            status = self.status()
            status["last_error"] = str(exc)
            status["message"] = f"UMU setup failed: {exc}"
            return status


def find_umu(paths: Paths) -> str:
    status = UMUManager(paths).status()
    if not status["available"]:
        raise BackendError("UMU setup is not ready yet. Forest will download it automatically; connect to the internet and try again.", "missing_umu")
    return status["path"]


def ensure_umu(paths: Paths) -> str:
    status = UMUManager(paths).prepare(required=True)
    if not status["available"]:
        raise BackendError(status["message"], "umu_setup")
    return status["path"]
