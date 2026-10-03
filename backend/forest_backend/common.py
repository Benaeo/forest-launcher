from dataclasses import dataclass
import os
from pathlib import Path


class BackendError(Exception):
    def __init__(self, message: str, code: str = "invalid_request"):
        super().__init__(message)
        self.code = code


def expand_path(value: str) -> str:
    return os.path.abspath(os.path.expandvars(os.path.expanduser(value))) if value else ""


def xdg_home(key: str, fallback: Path) -> Path:
    value = os.environ.get(key, "")
    return Path(value) if value and Path(value).is_absolute() else fallback


@dataclass(frozen=True)
class Paths:
    data: Path
    config: Path
    state: Path

    @classmethod
    def create(cls, root: str | None = None):
        if root:
            base = Path(root).expanduser().resolve()
            return cls(base / "data", base / "config", base / "state")
        home = Path.home()
        return cls(
            xdg_home("XDG_DATA_HOME", home / ".local/share") / "forest-launcher",
            xdg_home("XDG_CONFIG_HOME", home / ".config") / "forest-launcher",
            xdg_home("XDG_STATE_HOME", home / ".local/state") / "forest-launcher",
        )

    @property
    def database(self):
        return self.data / "library.sqlite3"

    @property
    def default_prefix_root(self):
        return self.data / "prefixes"
