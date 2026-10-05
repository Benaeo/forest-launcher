"""Read Steam online-fix identity without modifying the game's files."""

import configparser
import os
from pathlib import Path
import re
import stat

from .common import BackendError, expand_path


INI_NAMES = {"onlinefix.ini", "steamfix.ini"}
MAX_INI_BYTES = 1024 * 1024
MAX_FOLDER_ENTRIES = 8192


def read_fake_app_id(path: Path) -> str:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode):
                raise BackendError(f"Online-fix configuration is not a regular file: {path}", "invalid_online_fix_ini")
            if info.st_size > MAX_INI_BYTES:
                raise BackendError(f"Online-fix configuration is too large: {path}", "invalid_online_fix_ini")
            data = source.read(MAX_INI_BYTES + 1)
        if len(data) > MAX_INI_BYTES:
            raise BackendError(f"Online-fix configuration is too large: {path}", "invalid_online_fix_ini")
        encoding = "utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig"
        parser = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=(";", "#"))
        parser.optionxform = str.casefold
        parser.read_string(data.decode(encoding))
    except (OSError, UnicodeError, configparser.Error) as error:
        raise BackendError(
            f"Cannot read online-fix configuration {path}. Check its permissions, encoding, and INI syntax.",
            "invalid_online_fix_ini",
        ) from error

    # FakeAppId must be explicitly provided by [Main], not inherited from [DEFAULT].
    parser.defaults().clear()
    sections = [section for section in parser.sections() if section.strip().casefold() == "main"]
    value = parser.get(sections[0], "FakeAppId", fallback="").strip() if len(sections) == 1 else ""
    if not re.fullmatch(r"[0-9]{1,10}", value) or not 0 < int(value) <= 0xFFFFFFFF:
        raise BackendError(
            f"Online-fix requires a positive numeric FakeAppId in [Main] of {path}. "
            "Forest will not guess a Steam App ID or change the file.",
            "invalid_online_fix_app_id",
        )
    return str(int(value))


def resolve_fake_app_id(executable: Path) -> str:
    """Resolve matching INIs beside the selected executable on every launch/preview."""
    try:
        candidates = []
        with os.scandir(executable.parent) as entries:
            for count, entry in enumerate(entries):
                if count >= MAX_FOLDER_ENTRIES:
                    raise BackendError("The executable folder is too large to inspect safely.", "invalid_online_fix_ini")
                if entry.name.casefold() in INI_NAMES:
                    candidates.append(Path(entry.path))
        candidates.sort(key=lambda path: path.name)
    except OSError as error:
        raise BackendError(
            f"Cannot inspect online-fix configuration beside {executable}.", "invalid_online_fix_ini",
        ) from error
    if not candidates:
        raise BackendError(
            f"Online-fix requires OnlineFix.ini or SteamFix.ini beside the selected executable: {executable}. "
            "Select the executable next to the fix's configuration; Forest will not guess a Steam App ID.",
            "missing_online_fix_ini",
        )
    identities = {read_fake_app_id(path) for path in candidates}
    if len(identities) != 1:
        names = ", ".join(path.name for path in candidates)
        raise BackendError(
            f"Online-fix configurations disagree on FakeAppId beside {executable}: {names}. "
            "Forest will not choose an arbitrary Steam App ID.",
            "ambiguous_online_fix_app_id",
        )
    return identities.pop()


def detect_support(executable: str) -> dict:
    """Bounded, read-only eligibility check; DLL names alone are not evidence."""
    try:
        if not isinstance(executable, str) or not executable or "\0" in executable or len(executable) > 8192:
            raise BackendError("Choose a game executable to check Steam online-fix support.")
        path = Path(expand_path(executable))
        if not path.is_file():
            raise BackendError("Choose an existing game executable to check Steam online-fix support.")
        identity = resolve_fake_app_id(path)
        return {"supported": True, "fake_app_id": identity, "reason": ""}
    except (BackendError, OSError) as error:
        return {"supported": False, "fake_app_id": "", "reason": str(error)}


def effective_game(game: dict) -> dict:
    """Separate a saved checkbox preference from detected launch capability.

    Explicit legacy tags retain strict errors until edited into the new model.
    New/edited profiles opt in with online_fix_requested. Their preference is
    preserved, but unsupported games never use Steam online-fix mode. Detection
    is repeated on Save and every launch/preview; there is no persistent cache.
    """
    if "online_fix_requested" not in game:
        return game
    requested = game["online_fix_requested"]
    if type(requested) is not bool:
        raise BackendError("Online-fix preference must be true or false.")
    tags = [tag for tag in game.get("tags", []) if tag != "online-fix"]
    if requested and game.get("kind") == "windows" and detect_support(game.get("path", ""))["supported"]:
        tags.append("online-fix")
    return {**game, "tags": sorted(set(tags))}
