from datetime import datetime, timezone
import re
import shlex
from uuid import uuid4

from .common import BackendError, Paths, default_shared_prefix, default_game_prefix, expand_path, game_name
from .steam import DEFAULT_PROTON
from .lossless import default_options as default_lossless_options, validate_options as validate_lossless_options, valid_dll
from .artwork import api_key, validate_artwork, persisted_artwork, remove_saved_artwork, KINDS as ARTWORK_KINDS
from .steamshortcuts import selected_accounts
from .onlinefix import effective_game
from .jsonfiles import private_directory, read_document, write_document
from .operations import library_operation


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
    return {"kind": "windows", "prefix": "", "proton": "default",
            "arguments": "", "environment": {}, "tags": [], "online_fix_requested": False,
            "steam_launch_account": "", "steam_shortcut": False, "steam_accounts": [],
            "lossless_scaling": default_lossless_options(),
            **dict.fromkeys((*LAUNCH_TOGGLES, *SHORTCUT_TOGGLES), False)}


def validate_steam_defaults(value: dict) -> dict:
    """Validate the default snapshot without writing Steam or editing games."""
    shortcut = value.get("steam_shortcut", False)
    if type(shortcut) is not bool:
        raise BackendError("Steam shortcut default must be true or false.")
    accounts = selected_accounts(value.get("steam_accounts", []))
    if shortcut and not accounts:
        raise BackendError("Select at least one Steam account for the shortcut default.")
    account = value.get("steam_launch_account", "")
    if account != "":
        account = selected_accounts([account])[0]
        if not 0 < int(account) <= 0xFFFFFFFF:
            raise BackendError("Choose a valid default Steam launch account.")
    return {"steam_shortcut": shortcut, "steam_accounts": accounts, "steam_launch_account": account}


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
    if "online_fix_requested" in value:
        if type(value["online_fix_requested"]) is not bool:
            raise BackendError("Online-fix preference must be true or false.")
        options["online_fix_requested"] = value["online_fix_requested"]
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
        private_directory(paths.games_directory)
        pending = paths.state / "pending-saves"
        if pending.is_dir() and any(pending.glob("*.json")):
            with library_operation(paths):
                self._recover_renames(pending)

    def _recover_renames(self, directory):
        if directory.is_symlink():
            raise BackendError("Pending-save directory must not be a symbolic link.")
        for record_path in directory.glob("*.json"):
            record = read_document(record_path)
            old_slug, new_slug = record.get("old_slug", ""), record.get("new_slug", "")
            if not isinstance(old_slug, str) or not isinstance(new_slug, str) or game_name(old_slug) != old_slug or game_name(new_slug) != new_slug:
                raise BackendError("Invalid interrupted game rename record.")
            old_file = self.paths.games_directory / (old_slug + ".json")
            new_file = self.paths.games_directory / (new_slug + ".json")
            if new_file.exists():
                new_game = read_document(new_file)
                if new_game.get("id") != record.get("game_id") or game_name(new_game.get("title", "")) != new_slug:
                    raise BackendError("Interrupted rename conflicts with another game; nothing was deleted.")
                if old_file.exists():
                    if read_document(old_file).get("id") != record.get("game_id"):
                        raise BackendError("Interrupted rename conflicts with the old game; nothing was deleted.")
                    old_file.unlink()
            else:
                if not old_file.exists() or read_document(old_file).get("id") != record.get("game_id"):
                    raise BackendError("An interrupted rename is missing its game document.")
                remove_saved_artwork(self.paths, {"slug": new_slug})
            record_path.unlink()

    def close(self):
        pass

    def list_games(self) -> list[dict]:
        games, identities = [], set()
        for filename in sorted(self.paths.games_directory.glob("*.json")):
            value = read_document(filename)
            supported = {*default_game_options(), "title", "path", "id", "slug",
                         "created_at", "last_launched", "steamgriddb_id", "artwork"}
            if set(value) - supported:
                raise BackendError(f"Unsupported game fields in {filename}.")
            game = {**default_game_options(), **value, **validate_game(value)}
            identity = text(value.get("id", ""), "Game ID", required=True, limit=128)
            if not re.fullmatch(r"[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}", identity):
                raise BackendError(f"Invalid internal game ID in {filename}.")
            slug = game_name(game["title"])
            if filename.stem != slug:
                raise BackendError(f"Game filename must match its title: rename {filename.name} to {slug}.json.")
            if identity in identities:
                raise BackendError(f"Duplicate game ID in {filename}.")
            identities.add(identity)
            game["slug"] = slug
            game["created_at"] = text(value.get("created_at", ""), "Creation date")
            game["last_launched"] = text(value.get("last_launched", ""), "Last launch date")
            game.update(validate_steam_defaults(game))
            identity = game.get("steamgriddb_id")
            if identity is not None and (type(identity) is not int or not 0 < identity <= 2147483647):
                raise BackendError(f"Invalid SteamGridDB identity in {filename}.")
            game["steamgriddb_id"] = identity
            art = value.get("artwork", {})
            if not isinstance(art, dict) or set(art) - {*ARTWORK_KINDS, "extracted_icon"}:
                raise BackendError(f"Invalid artwork fields in {filename}.")
            if any(not isinstance(item, str) or "\0" in item for item in art.values()):
                raise BackendError(f"Invalid artwork paths in {filename}.")
            game["artwork"] = {**dict.fromkeys((*ARTWORK_KINDS, "extracted_icon"), ""), **art}
            games.append(game)
        return sorted(games, key=lambda game: game["title"].casefold())

    def get_game(self, game_id: str) -> dict:
        game_id = text(game_id, "Game ID", required=True, limit=180)
        for game in self.list_games():
            if game_id in (game["id"], game["slug"]):
                return game
        raise BackendError("That game no longer exists in the library.", "not_found")

    def save_game(self, value: dict) -> dict:
        if not isinstance(value, dict):
            raise BackendError("Game must be an object.")
        identity = text(value.get("id", ""), "Game ID", limit=128)
        creating = not identity
        settings = self.get_settings()
        previous = None if creating else self.get_game(identity)
        base = settings["new_game_defaults"] if creating else previous
        combined = {**base, **value}
        if isinstance(value.get("lossless_scaling"), dict):
            combined["lossless_scaling"] = {**base["lossless_scaling"], **value["lossless_scaling"]}
        if "tags" in value and "online_fix_requested" not in value and isinstance(value["tags"], list):
            combined["online_fix_requested"] = any(isinstance(tag, str) and tag.strip().casefold() == "online-fix"
                                                   for tag in value["tags"])
        if combined.get("kind") != "windows" and "tags" not in value:
            combined["tags"] = [tag for tag in combined.get("tags", []) if tag != "online-fix"]
        game = validate_game(combined)
        game["online_fix_requested"] = combined.get("online_fix_requested", False)
        game = effective_game(game)
        slug = game_name(game["title"])
        identity = str(uuid4()) if creating else previous["id"]
        for other in self.list_games():
            if other["id"] != identity and (other["slug"] == slug or other["title"].casefold() == game["title"].casefold()):
                raise BackendError("A game with this title or filename already exists.", "duplicate_game")
        if game["kind"] == "windows":
            if not game["prefix"]:
                game["prefix"] = default_game_prefix(game["title"], settings)
            if game["proton"] in ("", "default"):
                game["proton"] = settings["default_proton"]
        selected = combined.get("steam_shortcut", False)
        if creating and game["kind"] == "steam" and "steam_shortcut" not in value:
            selected = False
        steam_values = validate_steam_defaults({**combined, "steam_shortcut": selected})
        if selected and game["kind"] == "steam":
            raise BackendError("Steam library games do not need an additional Steam shortcut.")
        game.update(steam_values)
        identity_sgdb = combined.get("steamgriddb_id")
        if identity_sgdb is not None and (type(identity_sgdb) is not int or not 0 < identity_sgdb <= 2147483647):
            raise BackendError("SteamGridDB game ID must be a positive integer.")
        game["steamgriddb_id"] = identity_sgdb
        game["artwork"] = validate_artwork(combined.get("artwork", {}), self.paths)
        game["artwork"] = {**dict.fromkeys((*ARTWORK_KINDS, "extracted_icon"), ""), **game["artwork"]}
        game.update({"id": identity, "slug": slug,
                     "created_at": now() if creating else previous["created_at"],
                     "last_launched": "" if creating else previous["last_launched"]})
        rename_record = None
        if previous and previous["slug"] != slug:
            rename_record = self.paths.state / "pending-saves" / (previous["slug"] + ".json")
            write_document(rename_record, {"game_id": identity, "old_slug": previous["slug"], "new_slug": slug})
        with persisted_artwork(self.paths, slug, game["artwork"]) as saved_artwork:
            game["artwork"] = saved_artwork
            write_document(self.paths.games_directory / (slug + ".json"), game)
        if rename_record:
            (self.paths.games_directory / (previous["slug"] + ".json")).unlink()
            rename_record.unlink()
        return game

    def delete_game(self, game_id: str):
        game = self.get_game(game_id)
        (self.paths.games_directory / (game["slug"] + ".json")).unlink()

    def mark_launched(self, game_id: str):
        game = self.get_game(game_id)
        game["last_launched"] = now()
        write_document(self.paths.games_directory / (game["slug"] + ".json"), game)

    def relocate_lossless_paths(self, old_path: str, new_path: str) -> None:
        old_path = expand_path(text(old_path, "Previous DLL location"))
        new_path = expand_path(text(new_path, "DLL location", required=True))
        if not valid_dll(new_path):
            raise BackendError("Choose an existing, readable lsfg-vk.dll location.", "missing_lossless_dll")
        changes = []
        settings = self.get_settings()
        defaults = settings["new_game_defaults"]["lossless_scaling"]
        if defaults["dll_path"] == old_path:
            settings["new_game_defaults"]["lossless_scaling"] = {**defaults, "dll_path": new_path}
            changes.append((self.paths.settings_file, settings))
        for game in self.list_games():
            if game["lossless_scaling"]["dll_path"] == old_path:
                game["lossless_scaling"] = {**game["lossless_scaling"], "dll_path": new_path}
                changes.append((self.paths.games_directory / (game["slug"] + ".json"), game))
        originals = {path: read_document(path) for path, _ in changes}
        written = []
        try:
            for path, document in changes:
                write_document(path, document)
                written.append(path)
        except BaseException:
            for path in reversed(written):
                write_document(path, originals[path])
            raise

    def get_settings(self) -> dict:
        defaults = {
            "prefix_directory": str(default_shared_prefix().parent),
            "prefix_naming": "title",
            "default_proton": DEFAULT_PROTON,
            "close_after_launch": False,
            "steamgriddb_api_key": "",
            "default_icon_source": "extracted",
            "new_game_defaults": default_game_options(),
        }
        try:
            saved = read_document(self.paths.settings_file)
        except FileNotFoundError:
            try:
                write_document(self.paths.settings_file, defaults, exclusive=True)
                return defaults
            except FileExistsError:
                saved = read_document(self.paths.settings_file)
        saved.pop("prefix_root", None)  # Obsolete development-only prefix fallback.
        if set(saved) - set(defaults):
            raise BackendError("settings.json contains unsupported settings.")
        template = saved.get("new_game_defaults", {})
        if not isinstance(template, dict) or set(template) - set(default_game_options()):
            raise BackendError("new_game_defaults must contain only supported game options in settings.json.")
        defaults.update({key: value for key, value in saved.items() if key != "new_game_defaults"})
        defaults["new_game_defaults"].update(template)
        defaults["new_game_defaults"]["lossless_scaling"] = {
            **default_lossless_options(), **template.get("lossless_scaling", {})} if isinstance(
                template.get("lossless_scaling", {}), dict) else template["lossless_scaling"]
        defaults["new_game_defaults"] = {**validate_game_options(defaults["new_game_defaults"]),
                                         **validate_steam_defaults(defaults["new_game_defaults"])}
        for key in ("prefix_directory", "default_proton"):
            defaults[key] = text(defaults[key], key, required=True)
        if defaults["prefix_naming"] not in ("title", "default"):
            raise BackendError("Prefix naming must be title or default.")
        if type(defaults["close_after_launch"]) is not bool:
            raise BackendError("Close after launch must be true or false.")
        defaults["steamgriddb_api_key"] = api_key(defaults["steamgriddb_api_key"])
        if defaults["default_icon_source"] not in ("extracted", "steamgriddb"):
            raise BackendError("Default icon source must be extracted or steamgriddb.")
        defaults["prefix_directory"] = expand_path(defaults["prefix_directory"])
        if defaults["default_proton"] in ("", "default", "auto"):
            defaults["default_proton"] = DEFAULT_PROTON
        # Prefix preferences are separate from the per-game options template.
        defaults["new_game_defaults"]["prefix"] = ""
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
        if "default_icon_source" in values:
            source = values["default_icon_source"]
            if not isinstance(source, str) or source not in ("extracted", "steamgriddb"):
                raise BackendError("Default icon source must be extracted or steamgriddb.")
        if "prefix_naming" in values and values["prefix_naming"] not in ("title", "default"):
            raise BackendError("Prefix naming must be title or default.")
        for key in ("prefix_directory", "default_proton"):
            if key in values:
                values[key] = text(values[key], key, required=True)
        if "new_game_defaults" in values:
            options = values["new_game_defaults"]
            if not isinstance(options, dict) or set(options) - set(default_game_options()):
                raise BackendError("New game defaults must contain only supported game options.")
            combined = {**settings["new_game_defaults"], **options}
            if "tags" in options and "online_fix_requested" not in options and isinstance(options["tags"], list):
                combined["online_fix_requested"] = any(isinstance(tag, str) and tag.strip().casefold() == "online-fix"
                                                       for tag in options["tags"])
            if isinstance(options.get("lossless_scaling"), dict):
                combined["lossless_scaling"] = {**settings["new_game_defaults"]["lossless_scaling"],
                                                **options["lossless_scaling"]}
            options = {**validate_game_options(combined), **validate_steam_defaults(combined)}
            if options["proton"] not in ("", "default"):
                if "default_proton" in values and values["default_proton"] != options["proton"]:
                    raise BackendError("Default Proton selections must agree.")
                values["default_proton"] = options["proton"]
            options["proton"] = "default"
            options["prefix"] = ""
            values["new_game_defaults"] = options
        settings.update(values)
        settings["prefix_directory"] = expand_path(settings["prefix_directory"])
        write_document(self.paths.settings_file, settings)
        return settings
