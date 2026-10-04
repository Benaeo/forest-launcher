"""Launch a selected Windows file with the saved game's runner and prefix."""

from pathlib import Path

from .common import BackendError, expand_path
from .launch import build_plan, launch_game
from .storage import text

WINDOWS_FILES = (".exe", ".msi", ".bat", ".lnk", ".reg")


def file_plan(game, settings, paths, filename, *, prepare_components=False):
    if game["kind"] != "windows":
        raise BackendError("Run file in the prefix is only available for Windows games.", "no_prefix")
    selected = Path(expand_path(text(filename, "Windows file", required=True)))
    if not selected.is_file():
        raise BackendError("The selected Windows file does not exist.", "missing_executable")
    # Prefix tools do not inherit game launch arguments, tool toggles, or Steam fix mode.
    options = {**game, "path": str(selected), "arguments": "", "tags": [], "environment": {},
               "mangohud": False, "prefer_sdl": False, "no_sleep": False}
    plan = build_plan(options, settings, paths, prepare_components=prepare_components)
    suffix = selected.suffix.casefold()
    arguments = {
        ".reg": ["regedit", str(selected)],
        ".msi": ["msiexec", "/i", str(selected)],
        ".bat": ["cmd", "/c", str(selected)],
        ".lnk": ["start", "/unix", str(selected)],
    }.get(suffix, [str(selected)])
    plan.command = [plan.command[0], *arguments]
    game_directory = Path(game["path"]).parent
    plan.cwd = str(game_directory if game_directory.is_dir() else selected.parent)
    return plan


def run_file(game, settings, paths, filename):
    plan = file_plan(game, settings, paths, filename, prepare_components=True)
    return launch_game(game, settings, paths, plan=plan)
