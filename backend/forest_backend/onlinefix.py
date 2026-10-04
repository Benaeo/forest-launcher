"""Read Steam online-fix identity without modifying the game's files."""

import configparser
from pathlib import Path
import re

from .common import BackendError


INI_NAMES = {"onlinefix.ini", "steamfix.ini"}
MAX_INI_BYTES = 1024 * 1024


def read_fake_app_id(path: Path) -> str:
    try:
        if not path.is_file():
            raise BackendError(f"Online-fix configuration is not a regular file: {path}", "invalid_online_fix_ini")
        with path.open("rb") as source:
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
        candidates = sorted(
            (path for path in executable.parent.iterdir() if path.name.casefold() in INI_NAMES),
            key=lambda path: path.name,
        )
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
