import hashlib
import json
import os
from pathlib import Path
import re
import tempfile

from .common import BackendError, Paths, xdg_home, game_name
from .statefiles import ownership_path


GAME_ID = re.compile(r"^[A-Za-z0-9_-]{1,128}$")


def entry_text(value: str) -> str:
    return value.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def exec_argument(value: str) -> str:
    # Exec field codes and quoting are separate from desktop-entry string escaping.
    value = value.replace("%", "%%")
    value = re.sub(r'([\\"`$])', r'\\\1', value)
    return entry_text('"' + value + '"')


def desktop_directory() -> Path:
    home = Path.home()
    config = xdg_home("XDG_CONFIG_HOME", home / ".config") / "user-dirs.dirs"
    try:
        content = config.read_text()[:65536]
    except OSError:
        return home / "Desktop"
    match = re.search(r'^\s*XDG_DESKTOP_DIR="((?:\\.|[^"\\])*)"\s*$', content, re.MULTILINE)
    if not match:
        return home / "Desktop"
    value = re.sub(r'\\([\\"$`])', r'\1', match[1])
    value = value.replace("${HOME}", str(home)).replace("$HOME", str(home))
    path = Path(value)
    if not path.is_absolute() or path == home:
        raise BackendError("The Desktop directory is disabled or invalid. Set it in KDE or disable the Desktop shortcut.")
    return path


def atomic_write(path: Path, content: str, mode=0o600):
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".forest-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


class Shortcuts:
    def __init__(self, paths: Paths, game_id: str, slug: str):
        if not isinstance(game_id, str) or not GAME_ID.fullmatch(game_id):
            raise BackendError("Invalid game ID for shortcut.")
        self.paths = paths
        self.game_id = game_id
        self.manifest = ownership_path(paths, "shortcuts", game_id, slug)
        profile = hashlib.sha256(str(paths.data).encode()).hexdigest()
        self.marker = f"X-Forest-Owner={profile}:{game_id}\n"

    def metadata(self):
        try:
            with self.manifest.open("rb") as stream:
                value = json.loads(stream.read(65537))
            if not isinstance(value, dict) or not isinstance(value.get("paths", []), list):
                return {}
            return value
        except (OSError, ValueError):
            return {}

    def owned(self, path: Path) -> bool:
        if path.suffix != ".desktop" or path.is_symlink():
            return False
        try:
            with path.open(encoding="utf-8") as stream:
                return self.marker in stream.read(65536).splitlines(keepends=True)
        except (OSError, UnicodeError):
            return False

    def remove_path(self, path: Path):
        if self.owned(path):
            path.unlink()
        elif path.exists() or path.is_symlink():
            raise BackendError(f"Refusing to modify a shortcut not owned by this Forest profile: {path}")

    def remove(self):
        metadata = self.metadata()
        for value in metadata.get("paths", []):
            if isinstance(value, str) and Path(value).is_absolute():
                self.remove_path(Path(value))
        self.manifest.unlink(missing_ok=True)

    def sync(self, game: dict, context=None):
        metadata = self.metadata()
        title = re.sub(r"[\x00-\x1f/\\]", "-", game["title"]).strip()
        selected = [key for key in ("desktop_shortcut", "app_menu_shortcut") if game.get(key, False)]
        if not selected:
            self.remove()
            return []
        if "desktop_shortcut" in selected and len((title + ".desktop").encode("utf-8")) > 255:
            raise BackendError("The game title is too long for a shortcut filename.")
        filename = title + ".desktop"
        context = context if context is not None else metadata.get("context")
        if not isinstance(context, dict):
            raise BackendError("Shortcut creation requires the Forest executable location. Save this game in Forest's Add/Edit dialog.")
        for key in ("launcher", "backend"):
            value = context.get(key)
            if not isinstance(value, str) or not value or len(value) > 8192 or "\0" in value or not Path(value).is_absolute():
                raise BackendError("Invalid shortcut launcher context.")
        if not Path(context["launcher"]).is_file() or not os.access(context["launcher"], os.X_OK):
            raise BackendError("The Forest executable for the shortcut was not found or is not executable.")
        if not (Path(context["backend"]) / "forest_backend/__main__.py").is_file():
            raise BackendError("The Forest backend directory for the shortcut was not found.")
        arguments = [context["launcher"], "--backend-dir", context["backend"]]
        if self.paths.root:
            arguments += ["--data-root", str(self.paths.root)]
        arguments += ["--launch", self.game_id]
        content = ("[Desktop Entry]\nType=Application\nVersion=1.0\n"
                   f"Name={entry_text(game['title'])}\n"
                   f"Exec={' '.join(exec_argument(argument) for argument in arguments)}\n"
                   f"Icon={entry_text(game.get('artwork', {}).get('icon') or 'applications-games')}\nTerminal=false\nCategories=Game;\n" + self.marker)
        targets = []
        for key in selected:
            if self.paths.root:
                directory = self.paths.root / ("desktop" if key == "desktop_shortcut" else "applications")
            elif key == "desktop_shortcut":
                directory = desktop_directory()
            else:
                directory = xdg_home("XDG_DATA_HOME", Path.home() / ".local/share") / "applications"
            target_name = filename if key == "desktop_shortcut" else game_name(game["title"]) + ".desktop"
            targets.append(directory / target_name)
        old_paths = [Path(value) for value in metadata.get("paths", []) if isinstance(value, str) and Path(value).is_absolute()]
        # Persist cleanup information before publishing any files, including partial failures.
        all_paths = list(dict.fromkeys([*old_paths, *targets]))
        record = {"game_id": self.game_id, "context": context, "paths": [str(path) for path in all_paths]}
        atomic_write(self.manifest, json.dumps(record))
        for path, key in zip(targets, selected):
            if (path.exists() or path.is_symlink()) and not self.owned(path):
                raise BackendError(f"Refusing to overwrite a shortcut not owned by Forest: {path}")
            atomic_write(path, content, 0o755 if key == "desktop_shortcut" else 0o644)
        for path in old_paths:
            if path not in targets:
                self.remove_path(path)
        record["paths"] = [str(path) for path in targets]
        atomic_write(self.manifest, json.dumps(record))
        return record["paths"]
