from dataclasses import dataclass
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from .common import BackendError, Paths, expand_path, xdg_home
from .steam import discover_protons, ensure_native_steam, native_steam_root, runtime_command, steam_libraries


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


def find_umu(configured="") -> str:
    if configured:
        configured = expand_path(configured) if "/" in configured else configured
        result = shutil.which(configured)
        if result:
            return result
        raise BackendError("The configured UMU executable was not found or is not executable.", "missing_umu")
    result = shutil.which("umu-run")
    if result:
        return result
    # Reuse an existing Faugus UMU installation without importing its GTK code.
    data = xdg_home("XDG_DATA_HOME", Path.home() / ".local/share")
    cached = data / "faugus-launcher/umu-run"
    if cached.is_file() and os.access(cached, os.X_OK):
        return str(cached)
    raise BackendError("Install umu-launcher, or select an existing umu-run executable in Settings.", "missing_umu")


def resolve_proton(game: dict, settings: dict, steam_root: Path | None, *, native=False) -> str:
    selected = game["proton"]
    if selected in ("", "default"):
        selected = settings["default_proton"]
    builds = discover_protons(steam_root)
    if selected == "auto":
        if builds:
            return builds[0]["id"]
        if not native:
            return "GE-Proton"
        raise BackendError("Install a Proton build in Steam before using online-fix.", "missing_proton")
    for build in builds:
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

    def public(self) -> dict:
        return {
            "command": self.command, "environment": self.environment,
            "cwd": self.cwd, "log_path": str(self.log_path),
            "prefix": self.prefix, "mode": self.mode,
        }


def build_plan(game: dict, settings: dict, paths: Paths) -> LaunchPlan:
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
        return LaunchPlan([find_umu(settings["umu_program"]), str(executable), *arguments],
                          changes, str(executable.parent), log_path, str(prefix), "umu")

    if not steam_root or not shutil.which("steam"):
        raise BackendError("Online-fix requires an initialized native Steam installation.", "missing_steam")
    proton_dir = Path(proton)
    runtime = runtime_command(proton_dir, steam_libraries(steam_root))
    compat_data, wine_prefix = native_prefix_layout(prefix)
    files = {path.name.casefold() for path in executable.parent.iterdir() if path.is_file()}
    overrides = {name: mode for name, mode in DLL_OVERRIDES.items() if f"{name}.dll".casefold() in files}
    existing = changes.get("WINEDLLOVERRIDES", os.environ.get("WINEDLLOVERRIDES", ""))
    changes.update({
        "SteamAppId": "480", "SteamGameId": "480", "SteamOverlayGameId": "480",
        "UMU_USE_STEAM": "1", "WINEDLLOVERRIDES": merge_overrides(existing, overrides),
        "STEAM_COMPAT_CLIENT_INSTALL_PATH": str(steam_root),
        "STEAM_COMPAT_DATA_PATH": str(compat_data),
        "STEAM_COMPAT_INSTALL_PATH": str(executable.parent),
        "STEAM_COMPAT_SHADER_PATH": str(compat_data / "shadercache"),
        "STEAM_COMPAT_APP_ID": "480", "WINEPREFIX": str(wine_prefix),
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


def launch_game(game: dict, settings: dict, paths: Paths) -> dict:
    plan = build_plan(game, settings, paths)
    if plan.mode == "online-fix":
        ensure_native_steam()
        compat_data, _ = native_prefix_layout(Path(plan.prefix), create=True)
        (compat_data / "shadercache").mkdir(exist_ok=True)
    elif plan.mode == "umu":
        Path(plan.prefix).mkdir(parents=True, exist_ok=True)
    environment = {**os.environ, **plan.environment}
    if plan.mode == "online-fix":
        environment.pop("UMU_ID", None)
    plan.log_path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    descriptor = os.open(plan.log_path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
    with os.fdopen(descriptor, "ab", buffering=0) as log:
        log.write(("\nForest launch: " + shlex.join(plan.command) + "\n").encode("utf-8"))
        process = subprocess.Popen(
            plan.command, cwd=plan.cwd, env=environment, stdin=subprocess.DEVNULL,
            stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
        )
    return {**plan.public(), "pid": process.pid, "game_id": game["id"]}
