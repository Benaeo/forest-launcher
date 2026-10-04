"""Optional system lsfg-vk integration; never install it or modify its configuration."""

import os
from pathlib import Path
import shutil
import subprocess
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


def installed() -> bool:
    # Forest targets Arch/KDE. Query the package database, not a GUI executable
    # (or a stale DLL/configuration left over after uninstalling the package).
    pacman = shutil.which("pacman")
    if not pacman:
        return False
    try:
        return subprocess.run([pacman, "-Qq", "lsfg-vk"], stdin=subprocess.DEVNULL,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                              timeout=2, check=False).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
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
    return {"DISABLE_LSFGVK": "0", "LSFGVK_ENV": "1", "LSFGVK_DLL_PATH": str(dll),
            "LSFGVK_MULTIPLIER": str(options["multiplier"]),
            "LSFGVK_FLOW_SCALE": format(options["flow_scale"] / 100, ".2f"),
            "LSFGVK_PERFORMANCE_MODE": "1" if options["performance_mode"] else "0"}
