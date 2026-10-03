import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import time

from .common import BackendError, xdg_home


LATEST_PROTONS = ("Proton-CachyOS Latest", "Proton-GE Latest")
DEFAULT_PROTON = LATEST_PROTONS[0]


def proton_directory() -> Path:
    return Path.home() / ".local/share/Steam/compatibilitytools.d"


RUNTIMES = {
    "1391110": "SteamLinuxRuntime_soldier",
    "1628350": "SteamLinuxRuntime_sniper",
    "4183110": "SteamLinuxRuntime_4",
    "4185400": "SteamLinuxRuntime_4-arm64",
}


def native_steam_root() -> Path | None:
    candidates = [
        Path.home() / ".steam/steam",
        xdg_home("XDG_DATA_HOME", Path.home() / ".local/share") / "Steam",
    ]
    return next((path.resolve() for path in candidates if path.is_dir()), None)


def steam_libraries(steam_root: Path | None) -> list[Path]:
    if not steam_root:
        return []
    libraries = [steam_root]
    for name in ("steamapps/libraryfolders.vdf", "config/libraryfolders.vdf"):
        try:
            content = (steam_root / name).read_text(errors="replace")
        except OSError:
            continue
        for raw in re.findall(r'"path"\s*"((?:\\.|[^"\\])*)"', content):
            path = Path(re.sub(r'\\([\\"])', r"\1", raw))
            if path.is_dir() and path not in libraries:
                libraries.append(path)
    return libraries


def discover_protons(steam_root: Path | None = None) -> list[dict]:
    # Stable alias paths survive runner upgrades and symlink target changes.
    root = proton_directory()
    return [{"id": str(root / name), "label": name,
             "installed": (root / name / "proton").is_file()} for name in LATEST_PROTONS]


def discover_installed_protons(steam_root: Path | None = None) -> list[dict]:
    # Legacy automatic selections only; never exposed in normal runner selectors.
    roots = [
        Path.home() / ".local/share/Steam/compatibilitytools.d",
        Path.home() / ".var/app/com.valvesoftware.Steam/.local/share/Steam/compatibilitytools.d",
        Path("/usr/share/steam/compatibilitytools.d"),
    ]
    if steam_root:
        roots.insert(0, steam_root / "compatibilitytools.d")
    roots.extend(library / "steamapps/common" for library in steam_libraries(steam_root))
    found = {}
    for root in roots:
        try:
            entries = list(root.iterdir())
        except OSError:
            continue
        for directory in entries:
            if not (directory / "proton").is_file():
                continue
            path = str(directory.resolve())
            label = directory.name
            try:
                version = (directory / "version").read_text(errors="replace").strip().split(maxsplit=1)
                if len(version) == 2:
                    label = version[1]
            except OSError:
                pass
            found[path] = {"id": path, "label": label}
    def order(item):
        label = item["label"].casefold()
        ge = "ge-proton" in label or "proton-ge" in label
        version = tuple(-int(number) for number in re.findall(r"\d+", label)) or (0,)
        return (not ge, version, label)
    return sorted(found.values(), key=order)


def runtime_command(proton: Path, libraries: list[Path]) -> list[str]:
    manifest = proton / "toolmanifest.vdf"
    if not manifest.is_file():
        return []
    match = re.search(r'"require_tool_appid"\s*"(\d+)"', manifest.read_text(errors="replace"))
    if not match or match[1] == "0":
        return []
    app_id = match[1]
    name = RUNTIMES.get(app_id)
    if name:
        for library in libraries:
            path = library / "steamapps/common" / name / "run"
            if path.is_file():
                return [str(path), "--"]
    raise BackendError(
        f"This Proton build requires a Steam runtime. Install it in native Steam: steam://install/{app_id}",
        "missing_runtime",
    )


def steam_ready() -> bool:
    root = Path.home() / ".steam"
    try:
        pid = int((root / "steam.pid").read_text().strip())
        if pid <= 0 or Path(f"/proc/{pid}/comm").read_text().strip() != "steam":
            return False
        pipe = root / "steam.pipe"
        if not stat.S_ISFIFO(pipe.stat().st_mode):
            return False
        descriptor = os.open(pipe, os.O_WRONLY | os.O_NONBLOCK)
        os.close(descriptor)
        return True
    except (OSError, ValueError):
        return False


def ensure_native_steam(timeout=60):
    if steam_ready():
        return
    program = shutil.which("steam")
    if not program:
        raise BackendError("Install native Steam and sign in before using online-fix.", "missing_steam")
    environment = dict(os.environ)
    for key in list(environment):
        if key.startswith(("WINE", "PROTON", "UMU", "STEAM_COMPAT")) or key in (
            "SteamAppId", "SteamGameId", "SteamOverlayGameId", "GAMEID",
        ):
            environment.pop(key)
    subprocess.Popen([program, "-silent"], env=environment, stdin=subprocess.DEVNULL,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if steam_ready():
            return
        time.sleep(0.5)
    raise BackendError("Steam did not become ready. Open native Steam, sign in, and try again.", "steam_timeout")
