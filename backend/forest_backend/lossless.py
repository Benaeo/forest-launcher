"""Optional system lsfg-vk integration; never install it or modify its configuration."""

import json
import os
from pathlib import Path
import platform
import stat
import time

from .common import BackendError, expand_path

MISSING_PACKAGE = "Install the missing package lsfg-vk to use this feature."
MAX_SEARCH_ENTRIES = 100_000
SEARCH_SECONDS = 2.0


def default_options() -> dict:
    return {"dll_path": "", "multiplier": 1, "flow_scale": 100, "performance_mode": False}


def validate_options(value) -> dict:
    defaults = default_options()
    if not isinstance(value, dict) or set(value) - set(defaults):
        raise BackendError("Lossless Scaling settings must contain only supported options.")
    options = {**defaults, **value}
    path = options["dll_path"]
    if not isinstance(path, str) or "\0" in path or len(path) > 8192:
        raise BackendError("Lossless Scaling location must be valid text.")
    options["dll_path"] = expand_path(path.strip())
    for key, low, high in (("multiplier", 1, 100), ("flow_scale", 25, 100)):
        if type(options[key]) is not int or not low <= options[key] <= high:
            raise BackendError(f"Lossless Scaling {key} must be an integer between {low} and {high}.")
    if type(options["performance_mode"]) is not bool:
        raise BackendError("Lossless Scaling Performance Mode must be true or false.")
    return options


MAX_LAYER_DIRECTORIES = 32
MAX_LAYER_ENTRIES = 512
MAX_MANIFEST_BYTES = 64 * 1024
LAYER_SECONDS = 0.5
LAYER_NAME = "VK_LAYER_LSFGVK_frame_generation"


def path_list(value: str) -> list[Path]:
    # Ignore empty/relative entries instead of searching the process CWD.
    return [Path(item) for item in value[:32768].split(os.pathsep)[:MAX_LAYER_DIRECTORIES]
            if item and len(item) <= 4096 and "\0" not in item and Path(item).is_absolute()]


def layer_directories() -> list[Path]:
    # VK_LAYER_PATH controls EXPLICIT layers, not this implicit layer.
    if "VK_IMPLICIT_LAYER_PATH" in os.environ:
        return path_list(os.environ["VK_IMPLICIT_LAYER_PATH"])
    home = Path.home()
    config = path_list(os.environ.get("XDG_CONFIG_HOME", str(home / ".config")))
    data = path_list(os.environ.get("XDG_DATA_HOME", str(home / ".local/share")))
    roots = [*config, *path_list(os.environ.get("XDG_CONFIG_DIRS", "/etc/xdg")),
             Path("/etc"), Path("/usr/local/etc"), *data,
             *path_list(os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share"))]
    directories = [*path_list(os.environ.get("VK_ADD_IMPLICIT_LAYER_PATH", "")),
                   *(root / "vulkan/implicit_layer.d" for root in roots)]
    return list(dict.fromkeys(directories))[:MAX_LAYER_DIRECTORIES]


def library_directories() -> list[Path]:
    # Common dynamic-linker locations, not recursive home/disk discovery.
    machine = platform.machine().lower()
    triplet = "x86_64-linux-gnu" if machine in ("x86_64", "amd64") else "aarch64-linux-gnu"
    defaults = [Path(base) / triplet for base in ("/lib", "/usr/lib", "/usr/local/lib")]
    defaults += [Path(base) for base in ("/lib64", "/usr/lib64", "/lib", "/usr/lib", "/usr/local/lib")]
    return list(dict.fromkeys([*path_list(os.environ.get("LD_LIBRARY_PATH", "")), *defaults]))[:MAX_LAYER_DIRECTORIES]


def read_regular(path: Path, limit: int, *, whole=False) -> bytes:
    descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
    with os.fdopen(descriptor, "rb") as source:
        info = os.fstat(source.fileno())
        if not stat.S_ISREG(info.st_mode) or (whole and info.st_size > limit):
            raise ValueError("Not a bounded regular file")
        content = source.read(limit + 1 if whole else limit)
    if whole and len(content) > limit:
        raise ValueError("File grew beyond its read limit")
    return content


def compatible_library(path: Path) -> bool:
    try:
        # Shared-library symlinks are normal on Linux; resolve them, then open
        # the final target without following a raced replacement symlink.
        header = read_regular(path.resolve(strict=True), 64)
        machine = platform.machine().lower()
        expected = {"x86_64": 62, "amd64": 62, "aarch64": 183, "arm64": 183}.get(machine)
        return (expected is not None and len(header) == 64 and header[:6] == b"\x7fELF\x02\x01"
                and int.from_bytes(header[16:18], "little") == 3
                and int.from_bytes(header[18:20], "little") == expected)
    except (OSError, RuntimeError, ValueError):
        return False


def installed() -> bool:
    """Read-only v2-layer installation detection, independent of package managers.

    Never load a library, execute a CLI/UI, or read/write lsfg configuration.
    Installation evidence is not proof of GPU/game/runtime compatibility.
    """
    deadline = time.monotonic() + LAYER_SECONDS
    entries = 0
    for directory in layer_directories():
        if time.monotonic() >= deadline:
            return False
        try:
            with os.scandir(directory) as candidates:
                for candidate in candidates:
                    entries += 1
                    if entries > MAX_LAYER_ENTRIES or time.monotonic() >= deadline:
                        return False
                    if not candidate.name.endswith(".json"):
                        continue
                    try:
                        manifest = Path(candidate.path)
                        document = json.loads(read_regular(manifest, MAX_MANIFEST_BYTES, whole=True))
                        layer = document.get("layer") if isinstance(document, dict) else None
                        if not isinstance(layer, dict) or layer.get("name") != LAYER_NAME:
                            continue
                        # Forest uses the v2 per-launch environment API. Do not
                        # enable it for old layers with a different config API.
                        if str(layer.get("implementation_version")) != "2" or layer.get("type") != "GLOBAL":
                            continue
                        if layer.get("library_arch", "64") != "64":
                            continue
                        library = layer.get("library_path")
                        if not isinstance(library, str) or not library or len(library) > 4096 or "\0" in library:
                            continue
                        path = Path(library)
                        if path.is_absolute():
                            locations = [path]
                        elif "/" in library:
                            locations = [manifest.parent / path]
                        else:
                            locations = [root / path for root in library_directories()]
                        for location in locations:
                            if time.monotonic() >= deadline:
                                return False
                            if compatible_library(location):
                                return time.monotonic() < deadline
                    except (OSError, ValueError, RecursionError):
                        continue
        except OSError:
            continue
    return False


def discover_dll(home: Path | None = None) -> dict:
    """First match: Steam, steam, Games, Downloads. No recursive symlink following."""
    home = home if home is not None else Path.home()
    deadline = time.monotonic() + SEARCH_SECONDS
    visited = 0
    roots = [home / ".local/share" / name / "steamapps/common/Lossless Scaling"
             for name in ("Steam", "steam")]
    roots += [home / "Games", home / "Downloads"]
    for index, root in enumerate(roots):
        pending = [root]
        while pending:
            if time.monotonic() >= deadline:
                return {"dll_path": "", "limited": True}
            directory = pending.pop()
            try:
                with os.scandir(directory) as entries:
                    for entry in entries:
                        visited += 1
                        if visited > MAX_SEARCH_ENTRIES or time.monotonic() >= deadline:
                            return {"dll_path": "", "limited": True}
                        try:
                            if entry.name.casefold() == "lsfg-vk.dll" and entry.is_file():
                                if os.access(entry.path, os.R_OK):
                                    return {"dll_path": str(Path(entry.path).absolute()), "limited": False}
                            if index >= 2 and entry.is_dir(follow_symlinks=False):
                                pending.append(Path(entry.path))
                        except OSError:
                            continue
            except OSError:
                continue
    return {"dll_path": "", "limited": False}


def launch_environment(game: dict) -> dict[str, str]:
    # Old documents without this field keep their pre-existing launch behavior.
    if "lossless_scaling" not in game:
        return {}
    options = validate_options(game["lossless_scaling"])
    if game["kind"] == "steam":
        if options["multiplier"] > 1:
            raise BackendError("Lossless Scaling is not supported for Steam library launch requests. "
                               "Configure it in Steam, or use a direct game executable.", "unsupported_lossless_scaling")
        return {}
    if options["multiplier"] == 1:
        # v2.0's environment parser rejects multiplier=1. Disable at the loader.
        return {"DISABLE_LSFGVK": "1"}
    if not installed():
        raise BackendError(MISSING_PACKAGE, "missing_lsfg_vk")
    dll = Path(options["dll_path"]) if options["dll_path"] else None
    if dll is None or not dll.is_file() or not os.access(dll, os.R_OK):
        raise BackendError("Choose an existing, readable lsfg-vk.dll in the Lossless Scaling dialog.",
                           "missing_lossless_dll")
    if dll.name.casefold() != "lsfg-vk.dll":
        raise BackendError("Lossless Scaling location must point to lsfg-vk.dll, not Lossless.dll.",
                           "invalid_lossless_dll")
    # The loader disables an implicit layer for ANY non-empty disable value,
    # including "0". build_plan must unset DISABLE_LSFGVK in the child instead.
    return {"LSFGVK_ENV": "1", "LSFGVK_DLL_PATH": str(dll),
            "LSFGVK_MULTIPLIER": str(options["multiplier"]),
            "LSFGVK_FLOW_SCALE": format(options["flow_scale"] / 100, ".2f"),
            "LSFGVK_PERFORMANCE_MODE": "1" if options["performance_mode"] else "0"}
