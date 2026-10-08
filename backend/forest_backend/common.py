from dataclasses import dataclass
import os
from pathlib import Path
import re
import unicodedata


class BackendError(Exception):
    def __init__(self, message: str, code: str = "invalid_request"):
        super().__init__(message)
        self.code = code


def expand_path(value: str) -> str:
    return os.path.abspath(os.path.expandvars(os.path.expanduser(value))) if value else ""


def default_shared_prefix() -> Path:
    return Path.home() / "Games/forest-launcher/default"


def game_name(title: str) -> str:
    name = re.sub(r"[^\w-]+|_+", "-", unicodedata.normalize("NFKC", title).strip().lower()).strip("-.")
    if not name:
        raise BackendError("The game title must contain a letter or number.")
    if len(name.encode("utf-8")) > 180:
        raise BackendError("The game title is too long for a filename; shorten it.")
    return name


def default_game_prefix(title: str, settings: dict) -> str:
    name = game_name(title)
    if settings.get("prefix_naming", "title") == "default":
        name = "default"
    return str(Path(settings["prefix_directory"]) / name)


def xdg_home(key: str, fallback: Path) -> Path:
    value = os.environ.get(key, "")
    return Path(value) if value and Path(value).is_absolute() else fallback


@dataclass(frozen=True)
class Paths:
    data: Path
    config: Path
    state: Path
    root: Path | None = None

    @classmethod
    def create(cls, root: str | None = None):
        if root:
            base = Path(root).expanduser().resolve()
            return cls(base / "data", base / "config", base / "state", base)
        home = Path.home()
        return cls(
            xdg_home("XDG_DATA_HOME", home / ".local/share") / "forest-launcher",
            xdg_home("XDG_CONFIG_HOME", home / ".config") / "forest-launcher",
            xdg_home("XDG_STATE_HOME", home / ".local/state") / "forest-launcher",
        )

    @property
    def settings_file(self):
        return self.config / "settings.json"

    @property
    def games_directory(self):
        return self.data / "games"
