import shutil

from .common import BackendError, Paths
from .launch import build_plan, launch_game
from .onlinefix import detect_support
from .processes import running_games, stop_game
from .prefixes import removal_info, delete_prefix
from .prefixfiles import file_plan, run_file
from .operations import library_operation
from .steam import discover_protons, native_steam_root
from .storage import Store
from .shortcuts import Shortcuts
from . import artwork, news
from .artworktemp import cleanup_stale as cleanup_artwork
from .icons import extract_icon
from .steamshortcuts import accounts as steam_accounts, sync as sync_steam_shortcuts, steam_root as shortcut_steam_root
from .steamaccount import remembered_accounts, SteamRestartRequired
from .proton import list_releases, download_version, download_latest, cleanup_downloads, install_root
from .umu import UMUManager
from .lossless import installed as lsfg_installed, discover_dll, valid_dll, MISSING_PACKAGE


PROTOCOL_VERSION = 1


def validate_request(request):
    if (not isinstance(request, dict) or type(request.get("protocol")) is not int
            or request["protocol"] != PROTOCOL_VERSION):
        raise BackendError("Unsupported or missing backend protocol version.", "protocol_version")
    if not isinstance(request.get("params", {}), dict):
        raise BackendError("Request params must be an object.")


class Service:
    def __init__(self, paths: Paths, progress=lambda event: None):
        self.paths = paths
        self.progress = progress
        self.store = Store(paths)

    def close(self):
        self.store.close()

    def dispatch(self, request: dict) -> dict:
        validate_request(request)
        action = request.get("action")
        if action in ("save_game", "delete_game", "save_settings", "launch_game", "run_file", "stop_game", "relocate_lossless_scaling"):
            with library_operation(self.paths):
                return self._dispatch(request)
        return self._dispatch(request)

    def _dispatch(self, request):
        action = request.get("action")
        params = request.get("params", {})
        if action == "bootstrap":
            cleanup_downloads(self.paths)
            cleanup_artwork()
            settings = self.store.get_settings()
            umu = UMUManager(self.paths).status()
            account_error = ""
            try:
                switchable_accounts = remembered_accounts(shortcut_steam_root(self.paths))
            except BackendError as error:
                switchable_accounts = []
                account_error = str(error)
            return {
                "games": self.store.list_games(), "settings": settings,
                "steam_accounts": steam_accounts(self.paths),
                "steam_switchable_accounts": switchable_accounts,
                "steam_account_error": account_error,
                "running": running_games(self.paths),
                "protons": discover_protons(native_steam_root(), directory=install_root(self.paths)),
                "capabilities": {"umu": umu["path"], "steam": shutil.which("steam") or "",
                                 "lsfg_vk": lsfg_installed()},
                "umu": umu,
                "paths": {"data": str(self.paths.data), "state": str(self.paths.state)},
            }
        if action == "detect_online_fix":
            return detect_support(params.get("path"))
        if action == "extract_icon":
            return {"path": artwork.cache_bytes(self.paths, extract_icon(params.get("path")))}
        if action == "import_artwork":
            return artwork.import_image(self.paths, params.get("path"))
        if action == "artwork_download":
            return artwork.download_image(self.paths, params.get("url"))
        if action == "title_suggestions":
            return artwork.title_suggestions(self.store.get_settings()["steamgriddb_api_key"], params.get("query"))
        if action == "artwork_search":
            return artwork.search_games(self.store.get_settings()["steamgriddb_api_key"], params.get("query"), params.get("expanded", False))
        if action == "artwork_images":
            return artwork.images(self.store.get_settings()["steamgriddb_api_key"], params.get("game_id"), params.get("kind"), params.get("page", 0))
        if action == "discover_lossless_scaling":
            if not lsfg_installed():
                raise BackendError(MISSING_PACKAGE, "missing_lsfg_vk")
            return discover_dll()
        if action == "relocate_lossless_scaling":
            game = self.store.get_game(params.get("id", ""))
            old_path = game["lossless_scaling"]["dll_path"]
            if params.get("old_path") != old_path:
                raise BackendError("The saved DLL location changed. Cancel and launch again.", "dll_path_changed")
            self.store.relocate_lossless_paths(old_path, params.get("dll_path"))
            return {}
        if action == "news_releases":
            return news.releases(params.get("page", 1))
        if action == "news_images":
            return news.images(params.get("urls"))
        if action == "proton_releases":
            return list_releases(self.paths, params.get("family"), params.get("page", 1))
        if action == "download_latest_proton":
            return download_latest(self.paths, params.get("family"), self.progress)
        if action == "download_proton":
            return download_version(self.paths, params.get("family"), params.get("tag"), self.progress)
        if action == "prepare_umu":
            force = params.get("force", False)
            if type(force) is not bool:
                raise BackendError("Force update must be true or false.")
            return {"umu": UMUManager(self.paths).prepare(force=force)}
        if action == "list_games":
            return {"games": self.store.list_games()}
        if action == "save_game":
            value = params.get("game")
            previous = self.store.get_game(value["id"]) if isinstance(value, dict) and value.get("id") else None
            game = self.store.save_game(value)
            try:
                if previous and previous["slug"] != game["slug"]:
                    old_logs = self.paths.state / "logs" / previous["slug"]
                    new_logs = self.paths.state / "logs" / game["slug"]
                    if old_logs.is_dir() and not old_logs.is_symlink() and not new_logs.exists():
                        old_logs.rename(new_logs)
                files = Shortcuts(self.paths, game["id"], game["slug"]).sync(game, params.get("shortcut_context"))
                notice = sync_steam_shortcuts(self.paths, game, params.get("shortcut_context"))
                artwork.remove_saved_artwork(self.paths, previous or game, game["artwork"].values())
                return {"game": game, "shortcuts": files, "notice": notice}
            except (BackendError, OSError) as error:
                return {"game": game, "warning": f"Game saved, but shortcuts could not be updated: {error}"}
        if action == "running_games":
            return {"running": running_games(self.paths)}
        if action == "stop_game":
            return stop_game(self.paths, self.store.get_game(params.get("id", "")))
        if action == "removal_info":
            return removal_info(self.store.get_game(params.get("id", "")), self.store.get_settings(),
                                self.store.list_games(), self.paths)
        if action in ("run_file", "preview_file"):
            game = self.store.get_game(params.get("id", ""))
            settings = self.store.get_settings()
            if action == "preview_file":
                return file_plan(game, settings, self.paths, params.get("file")).public()
            return run_file(game, settings, self.paths, params.get("file"))
        if action == "delete_game":
            game_id = params.get("id", "")
            game = self.store.get_game(game_id)
            delete = params.get("delete_prefix", False)
            if type(delete) is not bool:
                raise BackendError("Delete prefix must be true or false.")
            if delete:
                info = removal_info(game, self.store.get_settings(), self.store.list_games(), self.paths)
                if not info["can_delete"]:
                    raise BackendError(info["reason"], "protected_prefix")
                expected = params.get("expected_prefix")
                if not isinstance(expected, dict) or any(expected.get(key) != info.get(key) for key in ("prefix", "device", "inode")):
                    raise BackendError("The prefix changed since confirmation. Reopen the removal dialog.", "prefix_changed")
            try:
                sync_steam_shortcuts(self.paths, game, remove=True)
                Shortcuts(self.paths, game["id"], game["slug"]).remove()
            except (BackendError, OSError) as error:
                raise BackendError(f"Could not remove shortcuts; the library entry was kept: {error}", "shortcut_error")
            if delete:
                delete_prefix(game, self.store.get_settings(), self.store.list_games(), self.paths,
                              params.get("expected_prefix"))
            self.store.delete_game(game_id)
            artwork.remove_saved_artwork(self.paths, game)
            return {}
        if action == "save_settings":
            return {"settings": self.store.save_settings(params.get("settings"))}
        if action in ("preview_launch", "launch_game"):
            game = self.store.get_game(params.get("id", ""))
            settings = self.store.get_settings()
            if action == "preview_launch":
                return build_plan(game, settings, self.paths).public()
            lossless = game["lossless_scaling"]
            if game["kind"] != "steam" and lossless["multiplier"] > 1 and not valid_dll(lossless["dll_path"]):
                if not lsfg_installed():
                    raise BackendError(MISSING_PACKAGE, "missing_lsfg_vk")
                found = discover_dll(home=self.paths.root) if self.paths.root else discover_dll()
                if valid_dll(found["dll_path"]):
                    self.store.relocate_lossless_paths(lossless["dll_path"], found["dll_path"])
                    game = self.store.get_game(game["id"])
                    settings = self.store.get_settings()
                else:
                    return {"lossless_confirmation": {"title": game["title"], "old_path": lossless["dll_path"]}}
            consent = params.get("steam_restart_consent")
            if consent is not None and (not isinstance(consent, dict)
                    or set(consent) != {"account", "session"}
                    or not all(isinstance(value, str) for value in consent.values())):
                raise BackendError("Steam restart consent must identify the account and Steam session.")
            try:
                result = launch_game(game, settings, self.paths, steam_restart_consent=consent)
            except SteamRestartRequired as required:
                # No process was spawned and no launch timestamp is recorded.
                # End this request/lock before the frontend opens its dialog.
                return {"steam_restart_confirmation": required.confirmation}
            self.store.mark_launched(game["id"])
            return result
        raise BackendError(f"Unknown backend action: {action!r}")
