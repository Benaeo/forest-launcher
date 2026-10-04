"""Deletion policy for explicit, individual Wine prefixes."""

from pathlib import Path
import shutil

from .common import BackendError, default_shared_prefix
from .processes import running_games


def effective_prefix(game, settings):
    if game["kind"] != "windows":
        return None
    return Path(game["prefix"] or (Path(settings["prefix_root"]) / game["id"]))


def canonical_prefix(path):
    resolved = path.resolve()
    # Proton's pfx directory and its compatibility-data parent share one prefix.
    return resolved.parent if resolved.name == "pfx" else resolved


def overlaps(first, second):
    return first == second or first in second.parents or second in first.parents


def removal_info(game, settings, games, paths):
    prefix = effective_prefix(game, settings)
    result = {"prefix": str(prefix) if prefix else "", "can_delete": False, "reason": "", "users": []}
    if prefix is None:
        result["reason"] = "This game type has no Wine prefix."
        return result
    resolved = canonical_prefix(prefix)
    users = [other for other in games
             if (other["kind"] == "windows" and overlaps(resolved, canonical_prefix(effective_prefix(other, settings))))
             or (other["id"] != game["id"] and other["kind"] != "steam"
                 and Path(other["path"]).resolve().is_relative_to(resolved))]
    result["users"] = [{"id": other["id"], "title": other["title"]} for other in users]
    defaults = [default_shared_prefix(), Path(settings["new_game_defaults"]["prefix"])]
    if any(str(default) != "." and overlaps(resolved, canonical_prefix(default)) for default in defaults):
        result["reason"] = "The shared/default prefix is protected; deleting it could affect other games."
    elif any(other["id"] != game["id"] for other in users):
        result["reason"] = "This prefix is shared with, contains, or is inside another game's prefix."
    elif any(resolved == protected or resolved in protected.parents for protected in
             (Path.home().resolve(), paths.data.resolve(), paths.config.resolve(), paths.state.resolve(),
              Path.cwd().resolve(), Path("/usr"), Path("/etc"), Path("/var"), Path("/opt"))):
        result["reason"] = "This location contains protected user, application, or system data."
    elif prefix.is_symlink():
        result["reason"] = "Deleting a prefix through a symlink is not supported."
    elif not prefix.is_dir():
        result["reason"] = "This prefix does not exist as a directory."
    elif not ((prefix / "drive_c").is_dir() or (prefix / "pfx/drive_c").is_dir()):
        result["reason"] = "This directory is not a recognizable Wine prefix; deletion is blocked."
    elif any(other["id"] in running_games(paths) for other in users):
        result["reason"] = "A game or prefix tool is still running in this prefix. Stop it before deleting the prefix."
    else:
        result["can_delete"] = True
    if prefix.is_dir():
        stat = prefix.stat()
        result.update({"device": stat.st_dev, "inode": stat.st_ino})
    return result


def delete_prefix(game, settings, games, paths, expected):
    info = removal_info(game, settings, games, paths)
    if not info["can_delete"]:
        raise BackendError(info["reason"], "protected_prefix")
    if not isinstance(expected, dict) or any(expected.get(key) != info.get(key) for key in ("prefix", "device", "inode")):
        raise BackendError("The prefix changed since confirmation. Reopen the removal dialog.", "prefix_changed")
    try:
        shutil.rmtree(info["prefix"])
    except OSError as error:
        raise BackendError("The prefix could not be fully removed; the library entry was kept. " + str(error), "prefix_delete_failed") from error
