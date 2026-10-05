"""Native Steam account selection; no passwords and no unconsented restarts.

Login evidence must belong to the live Steam process. Switching additionally
requires a fresh log event after startup. Steam-owned VDF files are validated
before editing; replacements preserve all unrelated text and private backups.
"""
from dataclasses import dataclass
from datetime import datetime
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import time

from .common import BackendError
from .steam import ensure_native_steam, native_steam_root, steam_client_running


ACCOUNT_ID = re.compile(r"[0-9]{1,10}")
STEAM_ID_BASE = 76561197960265728
MAX_VDF = 4 * 1024 * 1024
MAX_LOG_READ = 256 * 1024
ACCOUNT_TIMEOUT = 105


class SteamRestartRequired(BackendError):
    """A read-only result: the frontend must ask before retrying this launch."""
    def __init__(self, account: dict, session: str):
        super().__init__("Accept the Steam account switch before launching.", "steam_restart_required")
        self.confirmation = {"account": account["id"], "name": account["name"], "session": session}


@dataclass
class _Entry:
    name: str
    value: str | list
    start: int
    end: int


def _invalid_vdf():
    raise BackendError("Steam account data is malformed or ambiguous; no files were changed.",
                       "invalid_steam_accounts")


def _tokens(content: str):
    """Tokenize quoted strings, escapes, braces and comments, with source spans."""
    index = 1 if content.startswith("\ufeff") else 0
    while index < len(content):
        if content[index].isspace():
            index += 1
            continue
        if content.startswith("//", index):
            newline = content.find("\n", index)
            index = len(content) if newline < 0 else newline + 1
            continue
        start = index
        char = content[index]
        if char in "{}":
            index += 1
            yield char, char, start, index
            continue
        if char == '"':
            index += 1
            value = []
            while index < len(content) and content[index] != '"':
                char = content[index]
                if char == "\\":
                    index += 1
                    if index == len(content):
                        _invalid_vdf()
                    escaped = content[index]
                    char = {"\\": "\\", '"': '"', "n": "\n", "r": "\r", "t": "\t"}.get(escaped, "\\" + escaped)
                value.append(char)
                index += 1
            if index == len(content):
                _invalid_vdf()
            index += 1
            yield "text", "".join(value), start, index
        else:
            while index < len(content) and not content[index].isspace() and content[index] not in '{}"':
                index += 1
            yield "text", content[start:index], start, index


def _parse(content: str) -> list[_Entry]:
    tokens = iter(_tokens(content))
    count = 0

    def block(depth=0):
        nonlocal count
        if depth > 16:
            _invalid_vdf()
        entries, names = [], set()
        while True:
            token = next(tokens, None)
            if token is None:
                if depth:
                    _invalid_vdf()
                return entries, len(content)
            kind, name, _, end = token
            if kind == "}" and depth:
                return entries, end
            if kind != "text" or name.casefold() in names:
                _invalid_vdf()
            names.add(name.casefold())
            count += 1
            if count > 20000:
                _invalid_vdf()
            token = next(tokens, None)
            if token is None:
                _invalid_vdf()
            kind, value, start, end = token
            if kind == "{":
                value, end = block(depth + 1)
            elif kind != "text":
                _invalid_vdf()
            entries.append(_Entry(name, value, start, end))

    return block()[0]


def _entry(entries: list[_Entry], name: str) -> _Entry | None:
    return next((item for item in entries if item.name.casefold() == name.casefold()), None)


def _children(entries: list[_Entry], *names: str) -> list[_Entry]:
    for name in names:
        item = _entry(entries, name)
        if item is None or not isinstance(item.value, list):
            _invalid_vdf()
        entries = item.value
    return entries


def _field(entries: list[_Entry], name: str) -> str:
    item = _entry(entries, name)
    if item is None:
        return ""
    if not isinstance(item.value, str):
        _invalid_vdf()
    return item.value


def _users(content: str) -> list[_Entry]:
    users = _children(_parse(content), "users")
    for user in users:
        if not re.fullmatch(r"[0-9]{1,20}", user.name) or not isinstance(user.value, list):
            _invalid_vdf()
    return users


def _path(name: str, root: Path) -> Path:
    target = root / name
    if target.is_symlink() or target.parent.is_symlink():
        raise BackendError("Refusing to write through a Steam symbolic link.", "unsafe_steam_path")
    return target


def _read(path: Path) -> str:
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_VDF:
                raise BackendError("Steam account data is not a bounded regular file.", "invalid_steam_accounts")
            data = stream.read(MAX_VDF + 1)
            if len(data) > MAX_VDF:
                _invalid_vdf()
            return data.decode("utf-8", errors="surrogateescape")
    except FileNotFoundError:
        return ""
    except OSError as error:
        raise BackendError(f"Could not read Steam account data: {error}", "invalid_steam_accounts") from None


def _atomic_write(path: Path, content: str):
    # Preserve non-UTF-8 bytes in unrelated Steam fields as well as UTF-8 text.
    descriptor, temporary = tempfile.mkstemp(prefix=".forest-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(content.encode("utf-8", errors="surrogateescape"))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def _write(path: Path, content: str):
    """Back up the current file once, then atomically replace it (mode 0600)."""
    _path(path.name, path.parent)
    backup = _path(path.name + ".forest-backup", path.parent)
    if path.exists() and not backup.exists():
        _atomic_write(backup, _read(path))
    _atomic_write(path, content)


def _steam_session() -> dict | None:
    """Identify this user's live Steam process, including its non-reusable start."""
    if not steam_client_running():
        return None
    try:
        pid = int((Path.home() / ".steam/steam.pid").read_text().strip())
        if pid <= 0 or os.stat(f"/proc/{pid}").st_uid != os.getuid():
            return None
        with open(f"/proc/{pid}/stat", encoding="utf-8") as stream:
            process = stream.read(4096)
        closing = process.rfind(")")
        if process[process.find("(") + 1:closing] != "steam":
            return None
        ticks = int(process[closing + 2:].split()[19])  # /proc stat field 22
        with open("/proc/stat", encoding="utf-8") as stream:
            boot = re.search(r"^btime ([0-9]+)$", stream.read(65536), re.MULTILINE)
        if boot is None:
            return None
        return {"id": f"{pid}:{ticks}", "started": int(boot[1]) + ticks / os.sysconf("SC_CLK_TCK")}
    except (OSError, ValueError, IndexError):
        return None


def _log_position(root: Path) -> dict:
    try:
        descriptor = os.open(root / "logs/connection_log.txt", os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            info = os.fstat(descriptor)
            if stat.S_ISREG(info.st_mode):
                return {"device": info.st_dev, "inode": info.st_ino, "offset": info.st_size}
        finally:
            os.close(descriptor)
    except OSError:
        pass
    return {}


def logged_in_account(root: Path | None = None, *, after: dict | None = None) -> str | None:
    """Use only complete, timestamped login events from the live Steam session.

    ``after`` also excludes all pre-startup bytes when verifying a restart.
    Unknown/untimestamped history is never treated as permission to launch.
    """
    session = _steam_session()
    root = root or native_steam_root()
    if session is None or root is None:
        return None
    try:
        descriptor = os.open(root / "logs/connection_log.txt", os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            info = os.fstat(descriptor)
            if not stat.S_ISREG(info.st_mode):
                return None
            start = max(0, info.st_size - MAX_LOG_READ)
            if (after and (info.st_dev, info.st_ino) == (after.get("device"), after.get("inode"))
                    and info.st_size >= after.get("offset", 0)):
                start = max(start, after["offset"])
            tail = os.pread(descriptor, MAX_LOG_READ, start)
            if start and os.pread(descriptor, 1, start - 1) != b"\n":
                tail = tail.partition(b"\n")[2]
        finally:
            os.close(descriptor)
    except OSError:
        return None
    tail = tail.rpartition(b"\n")[0].decode("utf-8", errors="replace")
    current = None
    for line in tail.splitlines():
        match = re.search(r"^\[([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:.]+)\].*?\[Logged (On|Off)[^\]]*\](.*)$",
                          line, re.IGNORECASE)
        if match is None:
            continue
        try:
            timestamp = datetime.fromisoformat(match[1]).timestamp()
        except (ValueError, OverflowError, OSError):
            continue
        if timestamp < session["started"] or timestamp > time.time() + 1:
            continue
        identity = re.search(r"\[U:1:([0-9]{1,10})\]", match[3])
        current = identity[1] if match[2].lower() == "on" and identity and int(identity[1]) > 0 else None
    # Reject PID/session changes while reading, not just stale log events.
    latest = _steam_session()
    return current if latest and latest["id"] == session["id"] else None


def remembered_accounts(root: Path | None = None) -> list[dict]:
    """Read local accounts and auto-login eligibility without modifying Steam."""
    root = root or native_steam_root()
    if root is None:
        return []
    content = _read(root / "config/loginusers.vdf")
    if not content:
        return []
    result = []
    for user in _users(content):
        identity = int(user.name) - STEAM_ID_BASE
        if not 0 < identity <= 0xFFFFFFFF:
            continue
        result.append({"id": str(identity),
                       "name": _field(user.value, "PersonaName") or _field(user.value, "AccountName") or user.name,
                       "account_name": _field(user.value, "AccountName"),
                       "remembered": _field(user.value, "RememberPassword") == "1",
                       "offline": _field(user.value, "WantsOfflineMode") == "1"})
    return result


def _quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t") + '"'


def _auto_login_content(content: str, account_name: str) -> str:
    if not account_name or any(char in account_name for char in ('"', "\n", "\r", "\0")):
        raise BackendError("Invalid Steam account name.", "invalid_steam_accounts")
    steam = _children(_parse(content), "Registry", "HKCU", "Software", "Valve", "Steam")
    item = _entry(steam, "AutoLoginUser")
    if item is None or not isinstance(item.value, str):
        raise BackendError("Steam's registry does not record an auto-login account; sign in through Steam first.",
                           "invalid_steam_accounts")
    return content[:item.start] + _quote(account_name) + content[item.end:]


def set_auto_login(account_name: str, home: Path | None = None):
    registry = _path("registry.vdf", (home or Path.home()) / ".steam")
    _write(registry, _auto_login_content(_read(registry), account_name))


def _most_recent_content(content: str, steam_id: str) -> str:
    users = _users(content)
    target = str(STEAM_ID_BASE + int(steam_id))
    if not any(user.name == target for user in users):
        raise BackendError("The selected Steam account is no longer saved.", "missing_steam_account")
    changes = []
    for user in users:
        selected = user.name == target
        item = _entry(user.value, "MostRecent")
        if item is not None:
            if not isinstance(item.value, str):
                _invalid_vdf()
            changes.append((item.start, item.end, _quote("1" if selected else "0")))
        elif selected:
            closing = user.end - 1
            changes.append((closing, closing, '\t\t"MostRecent"\t\t"1"\n\t'))
    for start, end, replacement in sorted(changes, reverse=True):
        content = content[:start] + replacement + content[end:]
    _parse(content)  # Never write an invalid result.
    return content


def mark_most_recent(steam_id: str, root: Path | None = None):
    root = root or native_steam_root()
    if root is None:
        raise BackendError("Native Steam installation was not found.", "missing_steam")
    path = _path("config/loginusers.vdf", root)
    _write(path, _most_recent_content(_read(path), steam_id))


def shutdown(timeout=30, *, session: str | None = None) -> bool:
    """Ask only the consented Steam session to exit cleanly; never kill it."""
    if not steam_client_running():
        return True
    current = _steam_session()
    if session is not None and (current is None or current["id"] != session):
        return False
    program = shutil.which("steam")
    if not program:
        return False
    environment = dict(os.environ)
    for key in list(environment):
        if key.startswith(("WINE", "PROTON", "UMU", "STEAM_COMPAT")) or key in (
            "SteamAppId", "SteamGameId", "SteamOverlayGameId", "GAMEID",
        ):
            environment.pop(key)
    try:
        subprocess.Popen([program, "-shutdown"], env=environment, stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError:
        return False
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if not steam_client_running():
            return True
        current = _steam_session()
        if session is not None and (current is None or current["id"] != session):
            return False
        time.sleep(0.5)
    return False


def launch_account(game: dict) -> str:
    if "online-fix" not in game.get("tags", []):
        return ""
    value = game.get("steam_launch_account", "")
    if not isinstance(value, str) or (value and (not ACCOUNT_ID.fullmatch(value) or not 0 < int(value) <= 0xFFFFFFFF)):
        raise BackendError("The saved Steam launch account is invalid. Edit the game to select an account.",
                           "invalid_steam_accounts")
    return value


def prepare_account(game: dict, root: Path | None = None, home: Path | None = None,
                    *, consent: dict | None = None) -> dict:
    """Prepare an account within one deadline; restarting a live client needs consent.

    Consent is transient and tied to the requested account AND live Steam
    process start identity. A changed session requires a new popup, not reuse
    of an earlier acceptance. A confirmation result performs no Steam writes.
    """
    deadline = time.monotonic() + ACCOUNT_TIMEOUT

    def remaining(limit):
        seconds = deadline - time.monotonic()
        if seconds <= 0:
            raise BackendError("Steam account preparation timed out; the game was not started.", "steam_account_unverified")
        return min(limit, seconds)

    wanted = launch_account(game)
    root = root or native_steam_root()
    if not wanted:
        ensure_native_steam(timeout=remaining(60))
        return {"switched": False, "account": logged_in_account(root)}
    account = next((item for item in remembered_accounts(root) if item["id"] == wanted), None)
    if account is None:
        raise BackendError("The selected Steam account is not saved on this computer. Sign in to it once, or choose another account.",
                           "missing_steam_account")
    if logged_in_account(root) == wanted:
        ensure_native_steam(timeout=remaining(60))
        if logged_in_account(root) == wanted:
            return {"switched": False, "account": wanted}
    if not account["remembered"] or not account["account_name"]:
        raise BackendError(f"Steam does not remember {account['name']}. Sign in to it once with “Remember password” before Forest can switch to it.",
                           "steam_password_required")
    session = _steam_session()
    if steam_client_running():
        if session is None:
            raise BackendError("Steam's running session could not be identified. Close Steam manually and try again.",
                               "steam_session_unknown")
        if not consent or consent.get("account") != wanted or consent.get("session") != session["id"]:
            raise SteamRestartRequired(account, session["id"])
    else:
        session = None
    registry = _path("registry.vdf", (home or Path.home()) / ".steam")
    loginusers = _path("config/loginusers.vdf", root)
    # Validate both files before stopping Steam, then read them again after it
    # exits, since shutdown may flush a newer copy of either configuration.
    _auto_login_content(_read(registry), account["account_name"])
    _most_recent_content(_read(loginusers), wanted)
    if session and not shutdown(timeout=remaining(30), session=session["id"]):
        raise BackendError("Steam did not close cleanly, so the account was not switched and the game was not started. Close Steam and try again.",
                           "steam_shutdown_failed")
    if steam_client_running():
        raise BackendError("Steam started again during account preparation. Try the launch again to confirm its new session.",
                           "steam_session_changed")
    registry_before = _read(registry)
    registry_after = _auto_login_content(registry_before, account["account_name"])
    loginusers_after = _most_recent_content(_read(loginusers), wanted)
    _write(registry, registry_after)
    try:
        _write(loginusers, loginusers_after)
    except Exception:
        # Do not leave a half-applied switch after an ordinary write failure.
        _write(registry, registry_before)
        raise
    position = _log_position(root)
    ensure_native_steam(timeout=remaining(60))
    while time.monotonic() < deadline:
        if logged_in_account(root, after=position) == wanted:
            return {"switched": True, "account": wanted}
        time.sleep(min(1, max(0, deadline - time.monotonic())))
    raise BackendError(f"Steam started, but a fresh sign-in to {account['name']} could not be verified. Sign in to it once, then try again.",
                       "steam_account_unverified")
