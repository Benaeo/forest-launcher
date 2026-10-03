import shutil

from .common import BackendError, Paths
from .launch import build_plan, find_umu, launch_game
from .steam import discover_protons, native_steam_root
from .storage import Store


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
            try:
                umu = find_umu(settings["umu_program"])
            except BackendError:
                umu = ""
            return {
                "games": self.store.list_games(), "settings": settings,
                "protons": discover_protons(native_steam_root()),
                "capabilities": {"umu": umu, "steam": shutil.which("steam") or ""},
                "paths": {"data": str(self.paths.data), "state": str(self.paths.state)},
            }
        if action == "list_games":
            return {"games": self.store.list_games()}
        if action == "save_game":
            return {"game": self.store.save_game(params.get("game"))}
        if action == "delete_game":
            self.store.delete_game(params.get("id", ""))
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
