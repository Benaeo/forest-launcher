"""Per-profile welcome/update state and offline, bundled release notes.

Only GUI bootstrap supplies a version. Headless launches never consume notices.
State is separate from settings and contains no credentials.
"""
from pathlib import Path
import re

from .common import BackendError
from .jsonfiles import read_document, write_document
from .operations import library_operation
from . import changelog, news

# Both locations resolve relative to this backend, never to a caller-supplied
# path: installed share/forest-launcher, or the source tree's docs directory.
ROOT = Path(__file__).resolve().parents[2]
CHANGELOG_FILES = (ROOT / "CHANGELOG.md", ROOT / "docs" / "CHANGELOG.md")


def version_key(value):
    if not isinstance(value, str) or len(value) > 64 or not re.fullmatch(r"\d+\.\d+\.\d+", value, flags=re.ASCII):
        raise BackendError("Launcher version must be major.minor.patch.")
    return tuple(int(part) for part in value.split("."))


def _load(paths):
    state = read_document(paths.state / "startup.json")
    if (set(state) != {"schema", "last_version", "welcome_complete", "pending_from"}
            or type(state["schema"]) is not int or state["schema"] != 1
            or type(state["welcome_complete"]) is not bool):
        raise BackendError("Invalid launcher startup state.", "invalid_startup_state")
    current = version_key(state["last_version"])
    pending = state["pending_from"]
    if not isinstance(pending, str) or (pending and (
            not state["welcome_complete"] or version_key(pending) >= current)):
        raise BackendError("Invalid pending update state.", "invalid_startup_state")
    return state


def _save(paths, state):
    write_document(paths.state / "startup.json", state)


def state_for_version(paths, version):
    version_key(version)
    try:
        state = _load(paths)
    except FileNotFoundError:
        raise BackendError("Startup state is missing. Restart Forest Launcher.", "startup_changed") from None
    if state["last_version"] != version:
        raise BackendError("The running launcher version changed. Restart Forest Launcher.", "startup_changed")
    return state


def load_release_notes():
    for path in CHANGELOG_FILES:
        try:
            if path.stat().st_size > 16 * 1024 * 1024:
                return []
            return changelog.entries(path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            continue
        except (OSError, ValueError):
            return []
    return []


def releases_between(previous, current):
    lower, upper = version_key(previous), version_key(current)
    selected = {}
    for entry in load_release_notes():
        if not isinstance(entry, dict):
            continue
        try:
            key = version_key(entry.get("version"))
        except BackendError:
            continue
        body = entry.get("body")
        if lower < key <= upper and isinstance(body, str):
            version = entry["version"]
            selected[key] = {"version": version, "url": f"{news.RELEASES}/tag/v{version}",
                             **news.prepare(body[:news.MAX_BODY])}
    if upper > lower and upper not in selected:
        selected[upper] = {"version": current, "url": f"{news.RELEASES}/tag/v{current}",
            **news.prepare("Release notes for this version were not bundled with this installation. "
                           "You can browse the release history in Help → News when online.")}
    return [selected[key] for key in sorted(selected)]


def check(paths, version):
    current = version_key(version)
    with library_operation(paths):
        try:
            state = _load(paths)
        except FileNotFoundError:
            # A missing tracking file alone is not evidence of a new profile.
            # Existing installations get a baseline without a misleading welcome.
            existing = paths.settings_file.exists() or any(paths.games_directory.glob("*.json"))
            state = {"schema": 1, "last_version": version,
                     "welcome_complete": existing, "pending_from": ""}
            _save(paths, state)
        else:
            old = version_key(state["last_version"])
            if current != old:
                if current > old and state["welcome_complete"]:
                    state["pending_from"] = state["pending_from"] or state["last_version"]
                else:
                    state["pending_from"] = ""
                state["last_version"] = version
                _save(paths, state)
        if not state["welcome_complete"]:
            return {"kind": "welcome", "current_version": version}
        if state["pending_from"]:
            return {"kind": "update", "previous_version": state["pending_from"], "current_version": version,
                    "releases": releases_between(state["pending_from"], version)}
        return {"kind": "none", "current_version": version}


def finish_welcome(paths, version):
    # Caller holds the library lock and has already saved/validated settings.
    state = state_for_version(paths, version)
    state["welcome_complete"] = True
    state["pending_from"] = ""
    _save(paths, state)


def dismiss_update(paths, version):
    # Caller holds the library lock; stale windows cannot dismiss newer updates.
    state = state_for_version(paths, version)
    state["pending_from"] = ""
    _save(paths, state)
