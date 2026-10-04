import shutil

from .common import BackendError, Paths
from .launch import build_plan, launch_game
from .processes import running_games, stop_game
from .prefixes import removal_info, delete_prefix
from .prefixfiles import file_plan, run_file
from .operations import library_operation
from .steam import discover_protons, native_steam_root
from .storage import Store
from .shortcuts import Shortcuts
from . import artwork
from .icons import extract_icon
from .proton import list_releases, download_version, download_latest, cleanup_downloads, install_root
from .umu import UMUManager
from .lossless import installed as lsfg_installed, discover_dll, MISSING_PACKAGE


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
        if action in ("save_game", "delete_game", "save_settings", "launch_game", "run_file", "stop_game"):
            with library_operation(self.paths):
                return self._dispatch(request)
        return self._dispatch(request)

    def _dispatch(self, request):
        action = request.get("action")
        params = request.get("params", {})
        if action == "bootstrap":
            cleanup_downloads(self.paths)
            settings = self.store.get_settings()
            umu = UMUManager(self.paths).status()
            return {
                "games": self.store.list_games(), "settings": settings,
                "running": running_games(self.paths),
                "protons": discover_protons(native_steam_root(), directory=install_root(self.paths)),
                "capabilities": {"umu": umu["path"], "steam": shutil.which("steam") or "",
                                 "lsfg_vk": lsfg_installed()},
                "umu": umu,
                "paths": {"data": str(self.paths.data), "state": str(self.paths.state)},
            }
        if action == "extract_icon":
            return {"path": artwork.cache_bytes(self.paths, extract_icon(params.get("path")))}
        if action == "import_artwork":
            return artwork.import_image(self.paths, params.get("path"))
        if action == "artwork_download":
            return artwork.download_image(self.paths, params.get("url"))
        if action == "artwork_search":
            return artwork.search_games(self.store.get_settings()["steamgriddb_api_key"], params.get("query"), params.get("expanded", False))
        if action == "artwork_images":
            return artwork.images(self.store.get_settings()["steamgriddb_api_key"], params.get("game_id"), params.get("kind"), params.get("page", 0))
        if action == "discover_lossless_scaling":
            if not lsfg_installed():
                raise BackendError(MISSING_PACKAGE, "missing_lsfg_vk")
            return discover_dll()
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
            game = self.store.save_game(params.get("game"))
            try:
                files = Shortcuts(self.paths, game["id"]).sync(game, params.get("shortcut_context"))
                return {"game": game, "shortcuts": files}
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
                Shortcuts(self.paths, game_id).remove()
            except (BackendError, OSError) as error:
                raise BackendError(f"Could not remove shortcuts; the library entry was kept: {error}", "shortcut_error")
            if delete:
                delete_prefix(game, self.store.get_settings(), self.store.list_games(), self.paths,
                              params.get("expected_prefix"))
            self.store.delete_game(game_id)
            return {}
        if action == "save_settings":
            return {"settings": self.store.save_settings(params.get("settings"))}
        if action in ("preview_launch", "launch_game"):
            game = self.store.get_game(params.get("id", ""))
            settings = self.store.get_settings()
            if action == "preview_launch":
                return build_plan(game, settings, self.paths).public()
            result = launch_game(game, settings, self.paths)
            self.store.mark_launched(game["id"])
            return result
        raise BackendError(f"Unknown backend action: {action!r}")
