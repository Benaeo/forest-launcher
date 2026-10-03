import shutil

from .common import BackendError, Paths
from .launch import build_plan, launch_game
from .steam import discover_protons, native_steam_root
from .storage import Store
from .shortcuts import Shortcuts
from .umu import UMUManager


PROTOCOL_VERSION = 1


def validate_request(request):
    if (not isinstance(request, dict) or type(request.get("protocol")) is not int
            or request["protocol"] != PROTOCOL_VERSION):
        raise BackendError("Unsupported or missing backend protocol version.", "protocol_version")
    if not isinstance(request.get("params", {}), dict):
        raise BackendError("Request params must be an object.")


class Service:
    def __init__(self, paths: Paths):
        self.paths = paths
        self.store = Store(paths)

    def close(self):
        self.store.close()

    def dispatch(self, request: dict) -> dict:
        validate_request(request)
        action = request.get("action")
        params = request.get("params", {})
        if action == "bootstrap":
            settings = self.store.get_settings()
            umu = UMUManager(self.paths).status()
            return {
                "games": self.store.list_games(), "settings": settings,
                "protons": discover_protons(native_steam_root()),
                "capabilities": {"umu": umu["path"], "steam": shutil.which("steam") or ""},
                "umu": umu,
                "paths": {"data": str(self.paths.data), "state": str(self.paths.state)},
            }
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
        if action == "delete_game":
            game_id = params.get("id", "")
            self.store.get_game(game_id)
            try:
                Shortcuts(self.paths, game_id).remove()
            except (BackendError, OSError) as error:
                raise BackendError(f"Could not remove shortcuts; the library entry was kept: {error}", "shortcut_error")
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
