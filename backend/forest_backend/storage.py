from datetime import datetime, timezone
import json
import re
import shlex
import sqlite3
from uuid import uuid4

from .common import BackendError, Paths, default_shared_prefix, expand_path
from .steam import DEFAULT_PROTON
from .lossless import default_options as default_lossless_options, validate_options as validate_lossless_options
from .artwork import api_key, validate_artwork
from .steamshortcuts import selected_accounts


ENVIRONMENT_KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
GAME_KINDS = {"windows", "native", "steam"}
LAUNCH_TOGGLES = ("mangohud", "prefer_sdl", "no_sleep")
SHORTCUT_TOGGLES = ("desktop_shortcut", "app_menu_shortcut")


def now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def text(value, name: str, *, required=False, limit=8192) -> str:
    if not isinstance(value, str) or "\0" in value or len(value) > limit:
        raise BackendError(f"{name} must be valid text.")
    value = value.strip()
    if required and not value:
        raise BackendError(f"{name} is required.")
    return value


def parse_environment(value) -> dict[str, str]:
    if isinstance(value, str):
        result = {}
        for line in value.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise BackendError("Environment entries must use KEY=value, one per line.")
            key, content = line.split("=", 1)
            result[key.strip()] = content.strip()
        value = result
    if not isinstance(value, dict) or len(value) > 100:
        raise BackendError("Environment must be a small object of KEY=value entries.")
    for key, content in value.items():
        if not isinstance(key, str) or not ENVIRONMENT_KEY.fullmatch(key):
            raise BackendError(f"Invalid environment variable name: {key!r}")
        if not isinstance(content, str) or "\0" in content or len(content) > 32768:
            raise BackendError(f"Invalid value for environment variable {key}.")
    return dict(value)


def default_game_options() -> dict:
    return {"kind": "windows", "prefix": str(default_shared_prefix()), "proton": "default",
            "arguments": "", "environment": {}, "tags": [],
            "lossless_scaling": default_lossless_options(),
            **dict.fromkeys((*LAUNCH_TOGGLES, *SHORTCUT_TOGGLES), False)}


def validate_game_options(value: dict) -> dict:
    if not isinstance(value, dict):
        raise BackendError("Game options must be an object.")
    options = {
        "kind": text(value.get("kind", "windows"), "Game type"),
        "prefix": expand_path(text(value.get("prefix", ""), "Prefix")),
        "proton": text(value.get("proton", "default"), "Proton build"),
        "arguments": text(value.get("arguments", ""), "Arguments", limit=32768),
        "environment": parse_environment(value.get("environment", {})),
        "lossless_scaling": validate_lossless_options(value.get("lossless_scaling", {})),
    }
    for key in (*LAUNCH_TOGGLES, *SHORTCUT_TOGGLES):
        selected = value.get(key, False)
        if type(selected) is not bool:
            raise BackendError(f"{key} must be true or false.")
        options[key] = selected
    if options["kind"] not in GAME_KINDS:
        raise BackendError("Unsupported game type.")
    try:
        shlex.split(options["arguments"])
    except ValueError as exc:
        raise BackendError(f"Invalid game arguments: {exc}") from None
    tags = value.get("tags", [])
    if not isinstance(tags, list) or len(tags) > 30:
        raise BackendError("Tags must be a list of up to 30 labels.")
    options["tags"] = sorted({text(tag, "Tag", required=True, limit=64).casefold() for tag in tags})
    if "online-fix" in options["tags"] and options["kind"] != "windows":
        raise BackendError("The online-fix tag is only supported for Windows games.")
    if options["kind"] != "windows":
        options["prefix"] = ""
        options["proton"] = "default"
        options["prefer_sdl"] = False
    if options["kind"] == "steam":
        options["environment"] = {}
        options.update(dict.fromkeys(LAUNCH_TOGGLES, False))
        options["lossless_scaling"]["multiplier"] = 1
    return options


def validate_game(value: dict) -> dict:
    if not isinstance(value, dict):
        raise BackendError("Game must be an object.")
    game = {
        **validate_game_options(value),
        "title": text(value.get("title", ""), "Title", required=True, limit=256),
        "path": text(value.get("path", ""), "Executable or Steam App ID", required=True),
    }
    if game["kind"] == "steam":
        if not re.fullmatch(r"[0-9]{1,10}", game["path"]) or not 0 < int(game["path"]) <= 0xFFFFFFFF:
            raise BackendError("Steam App ID must be a positive number.")
    else:
        game["path"] = expand_path(game["path"])
    return game


class Store:
    def __init__(self, paths: Paths):
        self.paths = paths
        paths.data.mkdir(parents=True, mode=0o700, exist_ok=True)
        self.connection = sqlite3.connect(paths.database, timeout=10)
        paths.database.chmod(0o600)
        self.connection.execute("PRAGMA foreign_keys = ON")
        schema_version = self.connection.execute("PRAGMA user_version").fetchone()[0]
        if schema_version > 1:
            self.close()
            raise BackendError("This library was created by a newer Forest version.", "database_version")
        self.connection.executescript("""
            CREATE TABLE IF NOT EXISTS games (
                id TEXT PRIMARY KEY,
                document TEXT NOT NULL,
                created_at TEXT NOT NULL,
                last_launched TEXT NOT NULL DEFAULT ''
            );
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            PRAGMA user_version = 1;
        """)

    def close(self):
        self.connection.close()

    @staticmethod
    def decode(row) -> dict:
        game = json.loads(row[1])
        return {**game, "id": row[0], "created_at": row[2], "last_launched": row[3]}

    def list_games(self) -> list[dict]:
        rows = self.connection.execute("SELECT id, document, created_at, last_launched FROM games")
        return sorted((self.decode(row) for row in rows), key=lambda game: game["title"].casefold())

    def get_game(self, game_id: str) -> dict:
        game_id = text(game_id, "Game ID", required=True, limit=128)
        row = self.connection.execute(
            "SELECT id, document, created_at, last_launched FROM games WHERE id = ?", (game_id,)
        ).fetchone()
        if not row:
            raise BackendError("That game no longer exists in the library.", "not_found")
        return self.decode(row)

    def save_game(self, value: dict) -> dict:
        if not isinstance(value, dict):
            raise BackendError("Game must be an object.")
        game_id = text(value.get("id", ""), "Game ID", limit=128)
        creating = not game_id
        if creating:
            settings = self.get_settings()
            combined = {**settings["new_game_defaults"], **value}
            if combined["kind"] != "windows" and "tags" not in value:
                combined["tags"] = [tag for tag in combined["tags"] if tag != "online-fix"]
            game = validate_game(combined)
            game_id = str(uuid4())
            if game["kind"] == "windows":
                if not game["prefix"]:
                    game["prefix"] = settings["new_game_defaults"]["prefix"] or str(default_shared_prefix())
                if game["proton"] in ("", "default"):
                    game["proton"] = settings["default_proton"]
        else:
            previous = self.get_game(game_id)
            game = validate_game(value)
            if "lossless_scaling" not in value and "lossless_scaling" not in previous:
                game.pop("lossless_scaling")
            if game["kind"] == "windows" and not game["prefix"] and (previous["prefix"] or previous["kind"] != "windows"):
                # Clearing an explicit prefix resets it; untouched legacy blanks stay legacy.
                game["prefix"] = self.get_settings()["new_game_defaults"]["prefix"] or str(default_shared_prefix())
        if "artwork" in value:
            game["artwork"] = validate_artwork(value["artwork"], self.paths)
        elif not creating and "artwork" in previous:
            game["artwork"] = previous["artwork"]
        if "steam_shortcut" in value or (not creating and "steam_shortcut" in previous):
            selected = value.get("steam_shortcut", previous.get("steam_shortcut", False) if not creating else False)
            if type(selected) is not bool:
                raise BackendError("Steam shortcut must be true or false.")
            game["steam_shortcut"] = selected
            game["steam_accounts"] = selected_accounts(value.get("steam_accounts", previous.get("steam_accounts", []) if not creating else []))
            if selected and not game["steam_accounts"]:
                raise BackendError("Select at least one Steam account for the shortcut.")
            if selected and game["kind"] == "steam":
                raise BackendError("Steam library games do not need an additional Steam shortcut.")
        document = json.dumps(game, ensure_ascii=False)
        with self.connection:
            if not creating:
                self.get_game(game_id)
                self.connection.execute("UPDATE games SET document = ? WHERE id = ?", (document, game_id))
            else:
                self.connection.execute(
                    "INSERT INTO games (id, document, created_at) VALUES (?, ?, ?)",
                    (game_id, document, now()),
                )
        return self.get_game(game_id)

    def delete_game(self, game_id: str):
        self.get_game(game_id)
        with self.connection:
            self.connection.execute("DELETE FROM games WHERE id = ?", (game_id,))

    def mark_launched(self, game_id: str):
        with self.connection:
            self.connection.execute("UPDATE games SET last_launched = ? WHERE id = ?", (now(), game_id))

    def get_settings(self) -> dict:
        defaults = {
            "prefix_root": str(self.paths.default_prefix_root),
            "default_proton": DEFAULT_PROTON,
            "close_after_launch": False,
            "steamgriddb_api_key": "",
            "new_game_defaults": default_game_options(),
        }
        for key, value in self.connection.execute("SELECT key, value FROM settings"):
            if key == "new_game_defaults":
                defaults[key].update(json.loads(value))
            elif key in defaults:
                defaults[key] = json.loads(value)
        if defaults["default_proton"] in ("", "default", "auto"):
            defaults["default_proton"] = DEFAULT_PROTON
        if defaults["new_game_defaults"]["kind"] == "windows" and not defaults["new_game_defaults"]["prefix"]:
            defaults["new_game_defaults"]["prefix"] = str(default_shared_prefix())
        return defaults

    def save_settings(self, values: dict) -> dict:
        if not isinstance(values, dict):
            raise BackendError("Settings must be an object.")
        values = dict(values)
        settings = self.get_settings()
        if set(values) - set(settings):
            raise BackendError("Unknown setting.")
        if "close_after_launch" in values and type(values["close_after_launch"]) is not bool:
            raise BackendError("Close after launch must be true or false.")
        if "steamgriddb_api_key" in values:
            values["steamgriddb_api_key"] = api_key(values["steamgriddb_api_key"])
        for key in ("prefix_root", "default_proton"):
            if key in values:
                values[key] = text(values[key], key, required=True)
        if "new_game_defaults" in values:
            options = values["new_game_defaults"]
            if not isinstance(options, dict) or set(options) - set(default_game_options()):
                raise BackendError("New game defaults must contain only supported game options.")
            combined = {**settings["new_game_defaults"], **options}
            if isinstance(options.get("lossless_scaling"), dict):
                combined["lossless_scaling"] = {**settings["new_game_defaults"]["lossless_scaling"],
                                                **options["lossless_scaling"]}
            options = validate_game_options(combined)
            if options["proton"] not in ("", "default"):
                if "default_proton" in values and values["default_proton"] != options["proton"]:
                    raise BackendError("Default Proton selections must agree.")
                values["default_proton"] = options["proton"]
            options["proton"] = "default"
            if options["kind"] == "windows" and not options["prefix"]:
                options["prefix"] = str(default_shared_prefix())
            values["new_game_defaults"] = options
        settings.update(values)
        settings["prefix_root"] = expand_path(settings["prefix_root"])
        with self.connection:
            for key, value in settings.items():
                self.connection.execute(
                    "INSERT INTO settings (key, value) VALUES (?, ?) "
                    "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                    (key, json.dumps(value)),
                )
        return settings
