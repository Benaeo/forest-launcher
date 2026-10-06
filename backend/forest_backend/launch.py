from dataclasses import dataclass
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from .common import BackendError, Paths, expand_path
from .onlinefix import resolve_fake_app_id, effective_game
from .lossless import launch_environment as lossless_environment
from .processes import MARKER, prepare_tracking, finish_tracking, running_games
from .steam import discover_installed_protons, discover_protons, native_steam_root, runtime_command, steam_libraries
from .steamaccount import prepare_account
from .umu import ensure_umu, find_umu


DLL_OVERRIDES = {
    "OnlineFix64": "n", "OnlineFix32": "n",
    "SteamOverlay64": "n", "SteamOverlay": "n",
    "steam_api64": "n", "steam_api": "n",
    "winmm": "n,b", "winhttp": "n,b", "dnet": "n",
}


def merge_overrides(existing: str, additions: dict[str, str]) -> str:
    entries = [entry.strip() for entry in existing.split(";") if entry.strip()]
    configured = {
        name.strip().casefold().lstrip("*").removesuffix(".dll")
        for entry in entries if "=" in entry
        for name in entry.split("=", 1)[0].split(",")
    }
    entries.extend(f"{name}={mode}" for name, mode in additions.items() if name.casefold() not in configured)
    return ";".join(entries)


def resolve_proton(game: dict, settings: dict, steam_root: Path | None, *, native=False) -> str:
    selected = game["proton"]
    if selected in ("", "default"):
        selected = settings["default_proton"]
    builds = discover_protons(steam_root)
    for build in builds:
        if selected in (build["id"], build["label"]):
            if not build["installed"]:
                raise BackendError(f'{build["label"]} is not installed. Install it at {build["id"]}.', "missing_proton")
            return build["id"]
    if selected == "auto":
        # Preserve explicitly stored legacy automatic selections.
        builds = discover_installed_protons(steam_root)
        if builds:
            return builds[0]["id"]
        if not native:
            return "GE-Proton"
        raise BackendError("Install a Proton build in Steam before using online-fix.", "missing_proton")
    for build in discover_installed_protons(steam_root):
        if selected in (build["id"], build["label"], Path(build["id"]).name):
            return build["id"]
    candidate = Path(expand_path(selected))
    if (candidate / "proton").is_file():
        return str(candidate)
    raise BackendError("The selected Proton build was not found. Choose another build.", "missing_proton")


def native_prefix_layout(prefix: Path, *, create=False) -> tuple[Path, Path]:
    if prefix.exists() and not prefix.is_dir():
        raise BackendError("The selected prefix is a file, not a directory.", "invalid_prefix")
    pfx = prefix / "pfx"
    if prefix.name == "pfx" and prefix.is_dir() and not pfx.exists() and not pfx.is_symlink():
        return prefix.parent, prefix
    if pfx.is_symlink() and not pfx.is_dir():
        raise BackendError("This prefix has an invalid pfx symlink. Choose a different prefix.", "invalid_prefix")
    if pfx.exists():
        if not pfx.is_dir():
            raise BackendError("This prefix has an invalid pfx entry. Choose a different prefix.", "invalid_prefix")
        if (prefix / "drive_c").exists() and pfx.resolve() != prefix.resolve():
            raise BackendError("This prefix has conflicting Wine/Proton layouts.", "invalid_prefix")
        return prefix, pfx.resolve()
    if create:
        prefix.mkdir(parents=True, exist_ok=True)
        pfx.symlink_to(".", target_is_directory=True)
    return prefix, prefix


@dataclass
class LaunchPlan:
    command: list[str]
    environment: dict[str, str]
    cwd: str
    log_path: Path
    prefix: str = ""
    mode: str = "native"
    unset_environment: tuple[str, ...] = ()

    def public(self) -> dict:
        result = {
            "command": self.command, "environment": self.environment,
            "cwd": self.cwd, "log_path": str(self.log_path),
            "prefix": self.prefix, "mode": self.mode,
        }
        if self.unset_environment:
            result["unset_environment"] = list(self.unset_environment)
        return result


def build_plan(game: dict, settings: dict, paths: Paths, *, prepare_components=False) -> LaunchPlan:
    lossless = lossless_environment(game)
    inhibitor = None
    if game["kind"] != "steam" and game.get("no_sleep", False):
        inhibitor = shutil.which("systemd-inhibit")
        if not inhibitor:
            raise BackendError("No sleep requires systemd-inhibit. Install systemd or turn off No sleep.",
                               "missing_inhibitor")
    plan = base_plan(game, settings, paths, prepare_components=prepare_components)
    plan.environment.update(lossless)
    if lossless.get("LSFGVK_ENV") == "1":
        # Removing only the game override is insufficient: the launcher may
        # also inherit this disable flag. Record removal for the child env.
        plan.environment.pop("DISABLE_LSFGVK", None)
        plan.unset_environment = ("DISABLE_LSFGVK",)
    if game["kind"] != "steam":
        if game.get("mangohud", False):
            plan.environment["MANGOHUD"] = "1"
        if game["kind"] == "windows" and game.get("prefer_sdl", False):
            plan.environment["PROTON_PREFER_SDL"] = "1"
        if inhibitor:
            plan.command = [inhibitor, "--what=sleep", "--who=Forest Launcher",
                            "--why=Game is running", "--mode=block", "--no-ask-password",
                            "--", *plan.command]
    return plan


def base_plan(game: dict, settings: dict, paths: Paths, *, prepare_components=False) -> LaunchPlan:
    game = effective_game(game)
    arguments = shlex.split(game["arguments"])
    changes = dict(game["environment"])
    log_path = paths.state / "logs" / game["id"] / "launch.log"
    if game["kind"] == "steam":
        steam = shutil.which("steam")
        if not steam:
            raise BackendError("Install the native Steam client to launch Steam library games.", "missing_steam")
        return LaunchPlan([steam, "-silent", "-applaunch", game["path"], *arguments],
                          {}, str(Path.home()), log_path, mode="steam")
    executable = Path(game["path"])
    if not executable.is_file():
        raise BackendError(f"Executable not found: {executable}", "missing_executable")
    if game["kind"] == "native":
        if not os.access(executable, os.X_OK):
            raise BackendError("The native executable does not have execute permission.", "not_executable")
        return LaunchPlan([str(executable), *arguments], changes, str(executable.parent), log_path)

    prefix = Path(game["prefix"] or (Path(settings["prefix_root"]) / game["id"]))
    steam_root = native_steam_root()
    online_fix = "online-fix" in game["tags"]
    proton = resolve_proton(game, settings, steam_root, native=online_fix)
    changes.update({"WINEPREFIX": str(prefix), "PROTONPATH": proton})
    if not online_fix:
        changes.setdefault("UMU_USE_STEAM", "0")
        umu = ensure_umu(paths) if prepare_components else find_umu(paths)
        return LaunchPlan([umu, str(executable), *arguments],
                          changes, str(executable.parent), log_path, str(prefix), "umu")

    if not steam_root or not shutil.which("steam"):
        raise BackendError("Online-fix requires an initialized native Steam installation.", "missing_steam")
    app_id = resolve_fake_app_id(executable)
    proton_dir = Path(proton)
    runtime = runtime_command(proton_dir, steam_libraries(steam_root))
    compat_data, wine_prefix = native_prefix_layout(prefix)
    files = {path.name.casefold() for path in executable.parent.iterdir() if path.is_file()}
    overrides = {name: mode for name, mode in DLL_OVERRIDES.items() if f"{name}.dll".casefold() in files}
    existing = changes.get("WINEDLLOVERRIDES", os.environ.get("WINEDLLOVERRIDES", ""))
    changes.update({
        "SteamAppId": app_id, "SteamGameId": app_id, "SteamOverlayGameId": app_id,
        "UMU_USE_STEAM": "1", "WINEDLLOVERRIDES": merge_overrides(existing, overrides),
        "STEAM_COMPAT_CLIENT_INSTALL_PATH": str(steam_root),
        "STEAM_COMPAT_DATA_PATH": str(compat_data),
        "STEAM_COMPAT_INSTALL_PATH": str(executable.parent),
        "STEAM_COMPAT_SHADER_PATH": str(compat_data / "shadercache"),
        "STEAM_COMPAT_APP_ID": app_id, "WINEPREFIX": str(wine_prefix),
        "STEAM_COMPAT_TOOL_PATHS": str(proton_dir),
    })
    if runtime:
        changes["STEAM_COMPAT_TOOL_PATHS"] += ":" + str(Path(runtime[0]).parent)
    overlays = [str(path) for path in (
        steam_root / "ubuntu12_32/gameoverlayrenderer.so",
        steam_root / "ubuntu12_64/gameoverlayrenderer.so",
    ) if path.is_file()]
    if overlays:
        existing = changes.get("LD_PRELOAD", os.environ.get("LD_PRELOAD", ""))
        changes["LD_PRELOAD"] = ":".join(([existing] if existing else []) + overlays)
        changes["ENABLE_VK_LAYER_VALVE_steam_overlay_1"] = "1"
    return LaunchPlan([*runtime, str(proton_dir / "proton"), "waitforexitandrun", str(executable), *arguments],
                      changes, str(executable.parent), log_path, str(prefix), "online-fix")


def launch_game(game: dict, settings: dict, paths: Paths, *, plan=None,
                steam_restart_consent=None) -> dict:
    # Service serializes profile operations across GUI/shortcut processes.
    # Reject duplicates before runner preparation, Steam changes or log writes.
    if game["kind"] != "steam" and game["id"] in running_games(paths):
        raise BackendError("This game is already running. Stop it before starting it again.", "game_already_running")
    plan = plan or build_plan(game, settings, paths, prepare_components=True)
    account = {}
    if plan.mode == "online-fix":
        # The plan re-detected capability; use its effective online-fix mode
        # even if this profile was last saved before the INI was added.
        account = prepare_account({**game, "tags": [*game["tags"], "online-fix"]}, consent=steam_restart_consent)
        compat_data, _ = native_prefix_layout(Path(plan.prefix), create=True)
        (compat_data / "shadercache").mkdir(exist_ok=True)
    elif plan.mode == "umu":
        Path(plan.prefix).mkdir(parents=True, exist_ok=True)
    environment = {**os.environ, **plan.environment}
    for key in plan.unset_environment:
        environment.pop(key, None)
    if plan.mode == "online-fix":
        environment.pop("UMU_ID", None)
    plan.log_path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    descriptor = os.open(plan.log_path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(descriptor, "ab", buffering=0) as log:
        log.write(("\nForest launch: " + shlex.join(plan.command) + "\n").encode("utf-8"))
        tracking = None
        if game["kind"] != "steam":
            tracking, record = prepare_tracking(paths, game["id"])
            environment[MARKER] = record["token"]
        try:
            process = subprocess.Popen(
                plan.command, cwd=plan.cwd, env=environment, stdin=subprocess.DEVNULL,
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
            )
        except Exception:
            if tracking:
                tracking.unlink(missing_ok=True)
            raise
        if tracking:
            finish_tracking(tracking, record, process.pid)
    return {**plan.public(), "pid": process.pid, "game_id": game["id"],
            "steam_account": account.get("account") if plan.mode == "online-fix" else None}
