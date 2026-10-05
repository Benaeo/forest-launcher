from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import re
import shutil
import tarfile
import tempfile
import time
import urllib.parse
import urllib.error
import urllib.request

from .common import BackendError, Paths
from .steam import proton_directory


FAMILIES = {'cachyos': 'CachyOS/proton-cachyos', 'ge': 'GloriousEggroll/proton-ge-custom'}
TAG = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.+-]{0,99}\Z')
MAX_ARCHIVE = 2 * 1024**3
MAX_UNPACKED = 12 * 1024**3
MAX_MEMBERS = 150000


def repository(family):
    if not isinstance(family, str) or family not in FAMILIES:
        raise BackendError('Choose Proton-CachyOS or Proton-GE.', 'proton_request')
    return FAMILIES[family]


def fetch_json(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'Forest-Launcher/0.1', 'Accept': 'application/vnd.github+json'})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            if not response.geturl().startswith('https://'):
                raise BackendError('Release metadata redirected to an insecure URL.')
            raw = response.read(8 * 1024**2 + 1)
        if len(raw) > 8 * 1024**2:
            raise BackendError('Release metadata exceeds the size limit.')
        return json.loads(raw)
    except (OSError, ValueError) as error:
        raise BackendError(f'Could not load Proton releases. Check your connection or GitHub rate limit: {error}', 'proton_network') from None


def asset_for(family, release):
    repo = repository(family)
    if not isinstance(release, dict) or release.get('draft') or release.get('prerelease'):
        return None
    tag = release.get('tag_name', '')
    if not isinstance(tag, str) or not TAG.fullmatch(tag):
        return None
    machine = platform.machine().lower()
    if machine not in ('x86_64', 'amd64', 'aarch64', 'arm64'):
        raise BackendError('Proton Manager supports x86_64 and aarch64 systems.')
    arch = 'x86_64' if machine in ('x86_64', 'amd64') else ('arm64' if family == 'cachyos' else 'aarch64')
    if family == 'cachyos':
        if not tag.startswith('cachyos-') or not tag.endswith('-slr'):
            return None
        expected = f'proton-{tag}-{arch}.tar.xz'
        label = 'Proton-CachyOS-' + tag.removeprefix('cachyos-')
    else:
        if not tag.startswith('GE-Proton'):
            return None
        expected = f'{tag}-{arch}.tar.gz'
        label = tag
    assets = release.get('assets', [])
    if not isinstance(assets, list):
        return None
    matches = [asset for asset in assets if isinstance(asset, dict) and asset.get('name') == expected]
    # Older GE x86_64 releases did not include an architecture suffix.
    if not matches and family == 'ge' and arch == 'x86_64':
        matches = [asset for asset in assets if isinstance(asset, dict) and asset.get('name') == f'{tag}.tar.gz']
    if len(matches) != 1:
        return None
    asset = matches[0]
    url, size, digest = asset.get('browser_download_url'), asset.get('size'), asset.get('digest')
    prefix = f'https://github.com/{repo}/releases/download/{tag}/'
    if not isinstance(url, str) or not url.startswith(prefix) or type(size) is not int or not 0 < size <= MAX_ARCHIVE:
        return None
    # Older releases can use their upstream SHA-512 checksum sidecar instead.
    checksum = next((item for item in assets if isinstance(item, dict)
                     and item.get('name') == asset['name'].removesuffix('.tar.xz').removesuffix('.tar.gz') + '.sha512sum'), None)
    sha256 = digest[7:] if isinstance(digest, str) and re.fullmatch(r'sha256:[0-9a-f]{64}', digest) else ''
    checksum_url = checksum.get('browser_download_url', '') if checksum else ''
    if not sha256 and not checksum_url.startswith(prefix):
        return None
    return {'tag': tag, 'label': label, 'filename': asset['name'], 'url': url, 'size': size,
            'sha256': sha256, 'checksum_url': checksum_url, 'family': family}


def install_root(paths):
    # Isolated developer/test profiles must never write to the real Steam installation.
    return paths.root / 'compatibilitytools.d' if paths.root else proton_directory()


def list_releases(paths, family, page=1):
    repo = repository(family)
    if type(page) is not int or not 1 <= page <= 100:
        raise BackendError('Invalid release page.')
    releases = fetch_json(f'https://api.github.com/repos/{repo}/releases?per_page=30&page={page}')
    if not isinstance(releases, list):
        raise BackendError('Invalid Proton release list.')
    versions = []
    for release in releases:
        asset = asset_for(family, release)
        if asset:
            path = install_root(paths) / asset['label']
            versions.append({'tag': asset['tag'], 'label': asset['label'], 'size': asset['size'],
                             'path': str(path), 'installed': (path / 'proton').is_file()})
    return {'versions': versions, 'more': len(releases) == 30, 'directory': str(install_root(paths))}


def open_download(request, timeout=30):
    for attempt in range(3):
        try:
            return urllib.request.urlopen(request, timeout=timeout)
        except urllib.error.HTTPError as error:
            retryable = error.code in (429, 500, 502, 503, 504)
            error.close()
            if not retryable or attempt == 2:
                raise
            time.sleep(1 + attempt * 2)


def checksum_for(asset):
    if asset['sha256']:
        return 'sha256', asset['sha256']
    request = urllib.request.Request(asset['checksum_url'], headers={'User-Agent': 'Forest-Launcher/0.1'})
    with open_download(request, timeout=20) as response:
        if not response.geturl().startswith('https://'):
            raise BackendError('Checksum redirected to an insecure URL.')
        text = response.read(65537).decode('utf-8')
    if len(text) > 65536:
        raise BackendError('Checksum file is too large.')
    for line in text.splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]{128})\s+\*?(.+)', line.strip())
        if match and match[2].removeprefix('./') == asset['filename']:
            return 'sha512', match[1].lower()
    raise BackendError('Release checksum does not name the selected archive.')


def download_archive(asset, path, progress):
    algorithm, expected = checksum_for(asset)
    digest = hashlib.new(algorithm)
    request = urllib.request.Request(asset['url'], headers={'User-Agent': 'Forest-Launcher/0.1'})
    started = time.monotonic()
    last = 0.0
    downloaded = 0
    progress({'phase': 'download', 'bytes': 0, 'total': asset['size'], 'speed': 0})
    with open_download(request, timeout=30) as response, path.open('wb') as output:
        if not response.geturl().startswith('https://'):
            raise BackendError('Proton download redirected to an insecure URL.')
        while True:
            chunk = response.read(256 * 1024)
            if not chunk:
                break
            downloaded += len(chunk)
            if downloaded > asset['size']:
                raise BackendError('Proton download exceeded the advertised size.')
            digest.update(chunk)
            output.write(chunk)
            now = time.monotonic()
            if now - last >= 0.15 or downloaded == asset['size']:
                progress({'phase': 'download', 'bytes': downloaded, 'total': asset['size'],
                          'speed': int(downloaded / max(now - started, 0.001))})
                last = now
    progress({'phase': 'verify', 'bytes': downloaded, 'total': asset['size']})
    if downloaded != asset['size'] or digest.hexdigest() != expected:
        raise BackendError('Proton download failed its size or checksum verification.', 'proton_checksum')


def checked_archive_path(path, runner_root):
    try:
        if not path.resolve().is_relative_to(runner_root):
            raise BackendError('Unsafe resolved path in Proton archive.')
    except (OSError, RuntimeError, ValueError) as error:
        raise BackendError(f'Invalid path in Proton archive: {error}') from None
    return path


def extract_member(archive, member, destination, runner_root):
    # Early Python 3.11 has no tarfile data filter. Extract only validated
    # entries ourselves; never apply archived ownership or privileged modes.
    path = checked_archive_path(destination / member.name, runner_root)
    if member.isdir():
        path.mkdir(mode=0o700, parents=True, exist_ok=True)
        return
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if member.isfile():
        with archive.extractfile(member) as source:
            descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC | os.O_NOFOLLOW, 0o600)
            with os.fdopen(descriptor, 'wb') as output:
                shutil.copyfileobj(source, output, 256 * 1024)
                output.flush()
                mode = member.mode & 0o755
                if not member.mode & 0o100:
                    mode &= ~0o111
                os.fchmod(output.fileno(), mode | 0o600)
                try:
                    os.utime(output.fileno(), (member.mtime, member.mtime))
                except (ValueError, OverflowError):
                    raise BackendError('Invalid timestamp in Proton archive.') from None
        return
    target = path.parent / member.linkname if member.issym() else destination / member.linkname
    target = checked_archive_path(target, runner_root)
    if member.islnk() and not target.is_file():
        raise BackendError('Proton archive hard link must reference an extracted regular file.')
    if path.exists() or path.is_symlink():
        path.unlink()
    if member.issym():
        os.symlink(member.linkname, path)
    else:
        # Resolve to a validated regular file, not a symlink inode. Hard links
        # retain that file's already-sanitized permissions and ownership.
        os.link(target.resolve(), path, follow_symlinks=False)


def extract_archive(archive_path, destination, progress):
    with tarfile.open(archive_path, 'r:*') as archive:
        members = []
        total = 0
        root = None
        for member in archive:
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or not name.parts:
                raise BackendError('Unsafe path in Proton archive.')
            if root is None:
                root = name.parts[0]
            if name.parts[0] != root:
                raise BackendError('Proton archive must contain one runner directory.')
            if not (member.isfile() or member.isdir() or member.issym() or member.islnk()):
                raise BackendError('Unsupported special file in Proton archive.')
            if member.issym() or member.islnk():
                target = posixpath.normpath(posixpath.join(str(name.parent), member.linkname)
                                           if member.issym() else member.linkname)
                if PurePosixPath(target).is_absolute() or PurePosixPath(target).parts[0] != root:
                    raise BackendError('Unsafe link in Proton archive.')
            if member.size < 0:
                raise BackendError('Invalid size in Proton archive.')
            total += member.size
            members.append(member)
            if len(members) > MAX_MEMBERS or total > MAX_UNPACKED:
                raise BackendError('Proton archive exceeds extraction limits.')
        if not root:
            raise BackendError('Proton archive is empty.')
        if shutil.disk_usage(destination).free < total + 128 * 1024**2:
            raise BackendError('Not enough free disk space to extract Proton.')
        runner_root = destination.resolve() / root
        for index, member in enumerate(members):
            extract_member(archive, member, destination, runner_root)
            if index % 200 == 0 or index + 1 == len(members):
                progress({'phase': 'extract', 'bytes': index + 1, 'total': len(members)})
        # A later symlink can change how an earlier link resolves. Reject
        # those escapes before publishing the private staging directory.
        for member in members:
            if member.issym():
                checked_archive_path(destination / member.name, runner_root)
    runner = destination / root
    if not (runner / 'proton').is_file() or not (runner / 'compatibilitytool.vdf').is_file():
        raise BackendError('The extracted download is not a Steam Proton runner.')
    if not os.access(runner / 'proton', os.X_OK):
        raise BackendError('The downloaded Proton launcher is not executable.')
    return runner


def latest_name(family):
    repository(family)
    return "Proton-CachyOS Latest" if family == "cachyos" else "Proton-GE Latest"


def temporary_root(paths):
    return paths.root / "tmp" if paths.root else Path("/tmp")


def temporary_prefix(paths):
    identity = hashlib.sha256(str(install_root(paths).resolve()).encode()).hexdigest()[:16]
    return f"forest-proton-{os.getuid()}-{identity}-"


def runtime_root(paths):
    if paths.root:
        return paths.root / "runtime"
    value = os.environ.get("XDG_RUNTIME_DIR", "")
    base = Path(value) if value and Path(value).is_absolute() else None
    if base and base.is_dir() and base.stat().st_uid == os.getuid():
        return base / "forest-launcher"
    return Path("/tmp") / f"forest-launcher-runtime-{os.getuid()}"


@contextmanager
def installation_lock(paths):
    runtime = runtime_root(paths)
    runtime.mkdir(parents=True, mode=0o700, exist_ok=True)
    if runtime.is_symlink() or runtime.stat().st_uid != os.getuid():
        raise BackendError("Unsafe Forest runtime directory.")
    lock = os.open(runtime / (temporary_prefix(paths) + "install.lock"),
                   os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    try:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise BackendError("Another Proton download is already running.", "proton_busy") from None
        yield
    finally:
        os.close(lock)


def clean_abandoned(paths):
    # Caller holds the destination lock; never clean another active download.
    for base, pattern in ((temporary_root(paths), temporary_prefix(paths) + "*"),
                          (install_root(paths), ".forest-proton-stage-*")):
        for path in base.glob(pattern):
            if path.is_dir() and not path.is_symlink() and path.stat().st_uid == os.getuid():
                shutil.rmtree(path)
    legacy = install_root(paths) / ".forest-proton-downloads"
    if legacy.is_dir() and not legacy.is_symlink() and legacy.stat().st_uid == os.getuid():
        entries = list(legacy.iterdir())
        if all(item.name == "install.lock" or item.name.startswith("stage-") for item in entries):
            lockfile = legacy / "install.lock"
            if lockfile.is_file() and not lockfile.is_symlink():
                descriptor = os.open(lockfile, os.O_RDWR | os.O_NOFOLLOW)
                try:
                    try:
                        fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        return
                    shutil.rmtree(legacy)
                finally:
                    os.close(descriptor)
            elif not lockfile.exists() and not lockfile.is_symlink():
                shutil.rmtree(legacy)


def cleanup_downloads(paths):
    try:
        with installation_lock(paths):
            clean_abandoned(paths)
    except (BackendError, OSError):
        pass  # Startup cleanup must not prevent opening a library or interrupt a download.


def download_latest(paths, family, progress=lambda event: None):
    repo = repository(family)
    alias = install_root(paths) / ('Proton-CachyOS Latest' if family == 'cachyos' else 'Proton-GE Latest')
    if (alias / 'proton').is_file():
        return {'path': str(alias), 'label': alias.name, 'already_installed': True}
    release = fetch_json(f'https://api.github.com/repos/{repo}/releases/latest')
    asset = asset_for(family, release)
    if not asset:
        raise BackendError('The latest upstream release has no supported, verified Proton download.')
    return download_version(paths, family, asset['tag'], progress, latest=True)


def download_version(paths, family, tag, progress=lambda event: None, *, latest=False):
    repo = repository(family)
    if not isinstance(tag, str) or not TAG.fullmatch(tag):
        raise BackendError('Invalid Proton release tag.')
    release = fetch_json(f'https://api.github.com/repos/{repo}/releases/tags/{urllib.parse.quote(tag, safe="")}')
    asset = asset_for(family, release)
    if not asset or asset['tag'] != tag:
        raise BackendError('This release has no supported, verified Proton download.')
    directory = install_root(paths)
    version = directory / asset['label']
    target = directory / latest_name(family) if latest else version
    directory.mkdir(parents=True, exist_ok=True)
    try:
        with installation_lock(paths):
            clean_abandoned(paths)
            if target.exists() or target.is_symlink():
                if latest and (target / 'proton').is_file():
                    return {'path': str(target), 'label': target.name, 'already_installed': True}
                raise BackendError(f'The destination already exists at {target}. Nothing was overwritten.')
            with tempfile.TemporaryDirectory(prefix='.forest-proton-stage-', dir=directory) as staging:
                staging = Path(staging)
                if latest and (version / 'proton').is_file() and (version / 'compatibilitytool.vdf').is_file():
                    # Copy an existing version rather than linking it or taking it away from other profiles.
                    progress({'phase': 'extract', 'bytes': 0, 'total': 0})
                    runner = staging / 'runner'
                    shutil.copytree(version, runner, symlinks=True)
                else:
                    temporary_root(paths).mkdir(parents=True, exist_ok=True)
                    if shutil.disk_usage(temporary_root(paths)).free < asset['size'] + 128 * 1024**2:
                        raise BackendError('Not enough space in /tmp to download Proton.')
                    with tempfile.TemporaryDirectory(prefix=temporary_prefix(paths), dir=temporary_root(paths)) as download:
                        payload = Path(download) / asset['filename']
                        download_archive(asset, payload, progress)
                        progress({'phase': 'extract', 'bytes': 0, 'total': 0})
                        runner = extract_archive(payload, staging, progress)
                    # Archive is deleted before publishing the installation.
                if target.exists() or target.is_symlink():
                    raise BackendError('Runner destination appeared during installation; nothing was replaced.')
                os.rename(runner, target)
            progress({'phase': 'done', 'bytes': asset['size'], 'total': asset['size']})
            return {'path': str(target), 'label': target.name, 'tag': tag}
    except (OSError, tarfile.TarError) as error:
        raise BackendError(f'Proton installation failed: {error}', 'proton_install') from None
