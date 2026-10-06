"""Forest-owned Steam shortcuts. Never stop Steam; warn users to restart it.

Writing while Steam is running is intentional per the chosen UX. Backup and
atomic replacement prevent partial writes, NOT Steam overwriting its in-memory
snapshot later. A successful sync that changes shortcuts returns the explicit
restart/race notice only while a Steam client is running; otherwise the notice
is empty because no restart is needed and nothing can overwrite the write.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import stat
import tempfile
from uuid import uuid4
import zlib

from .artwork import managed_image, read_image
from .common import BackendError
from .shortcuts import atomic_write, GAME_ID
from .steam import native_steam_root, steam_client_running

MAX_VDF = 16 * 1024 * 1024


def parse_binary(data):
    if len(data) > MAX_VDF: raise BackendError("Steam shortcuts database is too large.")
    cursor, count = 0, 0
    def string():
        nonlocal cursor
        end = data.find(b"\0", cursor)
        if end < 0 or end - cursor > 32768: raise BackendError("Invalid Steam shortcut string.")
        value = data[cursor:end].decode("utf-8", errors="surrogateescape")
        cursor = end + 1
        return value
    def obj(depth=0):
        nonlocal cursor, count
        result = {}
        if depth > 16: raise BackendError("Steam shortcuts nesting is too deep.")
        while cursor < len(data):
            kind = data[cursor]
            cursor += 1
            if kind == 8: return result
            name = string()
            count += 1
            if count > 20000 or name in result: raise BackendError("Invalid Steam shortcuts entries.")
            if kind == 0: value = obj(depth + 1)
            elif kind == 1: value = string()
            elif kind in (2, 3, 4, 6, 7):
                length = 8 if kind == 7 else 4
                if cursor + length > len(data): raise BackendError("Truncated Steam shortcuts.")
                value = data[cursor:cursor + length]
                cursor += length
            else: raise BackendError("Unsupported Steam shortcuts format; no files were changed.")
            result[name] = (kind, value)
        raise BackendError("Truncated Steam shortcuts object.")
    result = obj()
    if cursor != len(data): raise BackendError("Unexpected trailing Steam shortcut data.")
    return result


def encode_binary(value):
    def string(item):
        if "\0" in item: raise BackendError("Invalid Steam shortcut text.")
        return item.encode("utf-8", errors="surrogateescape") + b"\0"
    result = bytearray()
    for key, (kind, item) in value.items():
        result.extend(bytes([kind]) + string(key))
        result.extend(encode_binary(item) if kind == 0 else string(item) if kind == 1 else item)
    return bytes(result) + b"\x08"


def text_vdf(content):
    tokens = re.findall(r'"((?:\\.|[^"\\])*)"|([{}])', content)
    result, stack, pending = {}, [], None
    current = result
    for text, brace in tokens:
        if brace == "{":
            if pending is None or len(stack) >= 16: raise ValueError("Invalid VDF")
            child = {}
            current[pending] = child
            stack.append(current)
            current, pending = child, None
        elif brace == "}":
            if not stack or pending is not None: raise ValueError("Invalid VDF")
            current = stack.pop()
        else:
            text = re.sub(r'\\([\\"])', r'\1', text)
            if pending is None: pending = text
            else: current[pending], pending = text, None
    if stack or pending is not None: raise ValueError("Invalid VDF")
    return result


def steam_root(paths):
    return paths.root / "steam" if paths.root is not None else native_steam_root()


def accounts(paths):
    root = steam_root(paths)
    if root is None or not root.is_dir(): return []
    names = {}
    try:
        source = root / "config/loginusers.vdf"
        if source.stat().st_size <= 1024 * 1024:
            users = text_vdf(source.read_text()).get("users", {})
            for identity, user in users.items():
                if identity.isdecimal() and isinstance(user, dict):
                    names[str(int(identity) & 0xffffffff)] = user.get("PersonaName", user.get("AccountName", identity))[:128]
    except (OSError, ValueError, UnicodeError): pass
    result = []
    directory = root / "userdata"
    if directory.is_symlink(): return []
    try:
        for index, entry in enumerate(directory.iterdir()):
            if index >= 256: break
            # userdata/0 is not a selectable signed-in Steam account.
            if (re.fullmatch(r"[0-9]{1,10}", entry.name) and int(entry.name) > 0
                    and entry.is_dir() and not entry.is_symlink()):
                result.append({"id": entry.name, "name": names.get(entry.name, "Steam account " + entry.name)})
    except OSError: pass
    return sorted(result, key=lambda account: account["name"].casefold())


def selected_accounts(value):
    if not isinstance(value, list) or len(value) > 32 or any(not isinstance(item, str) or not re.fullmatch(r"[0-9]{1,10}", item) for item in value):
        raise BackendError("Choose valid Steam accounts.")
    return sorted(set(value))


def write_bytes(path, data):
    path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
    if path.is_symlink() or path.parent.is_symlink(): raise BackendError("Refusing to write through a Steam symbolic link.")
    descriptor, temporary = tempfile.mkstemp(prefix=".forest-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally: Path(temporary).unlink(missing_ok=True)


def read_database(path):
    if path.is_symlink(): raise BackendError("Steam shortcuts database must not be a symbolic link.")
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as stream:
            if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode): raise BackendError("Steam shortcuts must be a regular file.")
            data = stream.read(MAX_VDF + 1)
    except FileNotFoundError: return b"", {"shortcuts": (0, {})}
    result = parse_binary(data)
    if result.get("shortcuts", (-1,))[0] != 0: raise BackendError("Steam shortcuts database has no shortcuts object.")
    return data, result


def marker(paths, game_id):
    return "Forest:" + hashlib.sha256(str(paths.data).encode()).hexdigest() + ":" + game_id


def owned(entry, token):
    tags = entry.get("tags", (0, {}))
    return tags[0] == 0 and any(value == (1, token) for value in tags[1].values())


def quoted(value):
    if not isinstance(value, str) or "\0" in value or "\n" in value or "\r" in value:
        raise BackendError("Invalid Steam shortcut argument.")
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def sync(paths, game, context=None, *, remove=False):
    if not isinstance(game.get("id"), str) or not GAME_ID.fullmatch(game["id"]):
        raise BackendError("Invalid Steam shortcut game ID.")
    manifest = paths.data / "steam-shortcuts" / (game["id"] + ".json")
    try:
        descriptor = os.open(manifest, os.O_RDONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        with os.fdopen(descriptor, "rb") as stream:
            info = os.fstat(stream.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_size > 65536:
                raise BackendError("Invalid Steam shortcut ownership manifest.")
            previous = json.loads(stream.read(65537))
        if not isinstance(previous, dict): raise BackendError("Invalid Steam shortcut ownership manifest.")
    except FileNotFoundError: previous = {}
    except (ValueError, UnicodeError): raise BackendError("Could not read Steam shortcut ownership information.") from None
    targets = [] if remove or not game.get("steam_shortcut", False) else selected_accounts(game.get("steam_accounts", []))
    old_accounts = selected_accounts(previous.get("accounts", []))
    if not targets and not old_accounts: return ""
    root = steam_root(paths)
    if root is None: raise BackendError("Native Steam installation was not found.")
    available = {account["id"] for account in accounts(paths)}
    if set(targets + old_accounts) - available: raise BackendError("A selected Steam account is missing; shortcut files were not changed.")
    context = context or previous.get("context")
    if targets:
        if not isinstance(context, dict) or not all(isinstance(context.get(key), str) and Path(context[key]).is_absolute() for key in ("launcher", "backend")):
            raise BackendError("Steam shortcuts require the Forest executable location.")
        if not Path(context["launcher"]).is_file() or not os.access(context["launcher"], os.X_OK) or not (
                Path(context["backend"]) / "forest_backend/__main__.py").is_file():
            raise BackendError("Forest executable/backend for the Steam shortcut was not found.")
        executable = quoted(context["launcher"])
        args = ["--backend-dir", context["backend"]]
        if paths.root is not None: args += ["--data-root", str(paths.root)]
        args += ["--launch", game["id"]]
        options = " ".join(quoted(arg) for arg in args)
    token = marker(paths, game["id"])
    artwork = {key: managed_image(paths, value) for key, value in game.get("artwork", {}).items() if key in ("icon", "grid", "hero", "logo")}
    asset_data = {key: read_image(value) for key, value in artwork.items() if value} if targets else {}
    old_assets = previous.get("assets", {})
    old_appids = previous.get("appids", {})
    if not isinstance(old_appids, dict) or any(not isinstance(key, str) or type(value) is not int or not 0 <= value <= 0xffffffff for key, value in old_appids.items()):
        raise BackendError("Invalid Steam App ID ownership manifest.")
    if not isinstance(old_assets, dict): raise BackendError("Invalid Steam artwork ownership manifest.")
    actions, new_assets, appids = [], {}, {}
    snapshot_bytes = 0
    for account in sorted(set(targets + old_accounts)):
        config = root / "userdata" / account / "config"
        if config.is_symlink() or (config / "grid").is_symlink(): raise BackendError("Refusing to write through a Steam symbolic link.")
        database = config / "shortcuts.vdf"
        original, document = read_database(database)
        shortcuts = document["shortcuts"][1]
        own = [key for key, value in shortcuts.items() if value[0] == 0 and owned(value[1], token)]
        if account in old_accounts and not own:
            expected_id = old_appids.get(account)
            if expected_id is None:
                raise BackendError("Steam shortcut ownership is incomplete; restore a backup before retrying.")
            if any(value[0] == 0 and value[1].get("appid") == (2, struct.pack("<I", expected_id))
                   for value in shortcuts.values()):
                raise BackendError("A Forest Steam shortcut lost its ownership marker; refusing to overwrite it.")
            # Truly absent entries may be safely recreated (Steam overwrote an
            # in-memory snapshot, or a previous multi-account write was interrupted).
        existing_entry = shortcuts[own[0]][1] if own else None
        own_id = existing_entry.get("appid", (2, b""))[1] if existing_entry else b""
        if existing_entry and (existing_entry.get("appid", (None,))[0] != 2 or not isinstance(own_id, bytes) or len(own_id) != 4):
            raise BackendError("Invalid Forest Steam shortcut App ID.")
        for key in own: del shortcuts[key]
        writes = {}
        if account in targets:
            identity = struct.unpack("<I", own_id)[0] if len(own_id) == 4 else zlib.crc32((executable + game["title"]).encode()) | 0x80000000
            others = {struct.unpack("<I", entry[1]["appid"][1])[0] for entry in shortcuts.values()
                      if entry[0] == 0 and entry[1].get("appid", (None, b""))[0] == 2 and len(entry[1]["appid"][1]) == 4}
            if identity in others: raise BackendError("Steam shortcut App ID collision; existing shortcuts were not overwritten.")
            entry = dict(existing_entry or {})
            entry.update({"appid": (2, struct.pack("<I", identity)), "AppName": (1, game["title"]),
                          "Exe": (1, executable), "StartDir": (1, quoted(str(Path(context["launcher"]).parent))),
                          "icon": (1, artwork.get("icon", "")), "LaunchOptions": (1, options),
                          "ShortcutPath": (1, ""), "FlatpakAppID": (1, "")})
            for field, value in {"IsHidden": 0, "AllowDesktopConfig": 1, "AllowOverlay": 1, "OpenVR": 0, "Devkit": 0, "DevkitOverrideAppID": 0, "LastPlayTime": 0}.items():
                entry.setdefault(field, (2, struct.pack("<I", value)))
            tags = entry.setdefault("tags", (0, {}))[1]
            if not any(value == (1, token) for value in tags.values()): tags[str(len(tags))] = (1, token)
            next_key = 0
            while str(next_key) in shortcuts: next_key += 1
            shortcuts[str(next_key)] = (0, entry)
            appids[account] = identity
            for kind, data in asset_data.items():
                extension = Path(artwork[kind]).suffix
                suffixes = ("p", "") if kind == "grid" else ("_" + kind,)
                for suffix in suffixes: writes[str(identity) + suffix + extension] = data
        owned_files = old_assets.get(account, {})
        if not isinstance(owned_files, dict): raise BackendError("Invalid Steam artwork ownership manifest.")
        deletions = []
        for name, digest in owned_files.items():
            accepted_digests = digest if isinstance(digest, list) else [digest]
            if not accepted_digests or len(accepted_digests) > 4 or any(not isinstance(item, str) or not re.fullmatch(r"[a-f0-9]{64}", item) for item in accepted_digests):
                raise BackendError("Invalid Steam artwork ownership digest.")
            if not re.fullmatch(r"[0-9]+(?:p|_hero|_logo|_icon)?\.(png|jpg|webp|ico)", name): raise BackendError("Invalid Steam artwork manifest path.")
            target = config / "grid" / name
            if target.exists() or target.is_symlink():
                if target.is_symlink() or target.stat().st_size > MAX_VDF or hashlib.sha256(target.read_bytes()).hexdigest() not in accepted_digests:
                    raise BackendError("Steam artwork was changed outside Forest; refusing to overwrite or remove it.")
                if name not in writes: deletions.append(target)
        for name in writes:
            target = config / "grid" / name
            if target.exists() and name not in owned_files:
                raise BackendError("Steam artwork already exists and is not owned by Forest.")
        new_assets[account] = {name: hashlib.sha256(data).hexdigest() for name, data in writes.items()}
        updated = encode_binary(document)
        snapshot_bytes += len(original) + len(updated)
        if snapshot_bytes > 64 * 1024 * 1024:
            raise BackendError("Selected Steam shortcut databases exceed the operation size limit.")
        actions.append((database, original, updated, config, writes, deletions))
    # Recheck the original snapshot immediately before writing. This narrows but
    # cannot eliminate the running-Steam race acknowledged in the restart notice.
    for database, original, *_ in actions:
        if read_database(database)[0] != original: raise BackendError("Steam shortcuts changed during saving; try again.")
    # Journal ownership before publication so partial failures remain recoverable.
    recovery_assets = {}
    for account in sorted(set(targets + old_accounts)):
        before, after = old_assets.get(account, {}), new_assets.get(account, {})
        recovery_assets[account] = dict(before)
        for name, digest in after.items():
            old = before.get(name)
            known = (old if isinstance(old, list) else [old]) if old else []
            combined = list(dict.fromkeys(known + [digest]))
            if len(combined) > 4: raise BackendError("Too many interrupted artwork updates; restore a backup before retrying.")
            recovery_assets[account][name] = combined
    record = {"accounts": sorted(set(targets + old_accounts)), "context": context, "assets": recovery_assets, "appids": {**old_appids, **appids}}
    atomic_write(manifest, json.dumps(record))
    # A running client is checked around publication, not from cached startup
    # state, because it is what can overwrite the freshly written snapshot.
    steam_running = steam_client_running()
    for database, original, updated, config, writes, deletions in actions:
        if original:
            backup_root = paths.state / "steam-backups"
            write_bytes(backup_root / (database.parent.parent.name + "-" + uuid4().hex + ".vdf"), original)
            backups = sorted((path for path in backup_root.glob(database.parent.parent.name + "-*.vdf")
                              if path.is_file() and not path.is_symlink()), key=lambda path: path.stat().st_mtime_ns)
            for old_backup in backups[:-8]:
                if not old_backup.is_symlink(): old_backup.unlink(missing_ok=True)
        write_bytes(database, updated)
        for name, data in writes.items(): write_bytes(config / "grid" / name, data)
        for target in deletions: target.unlink(missing_ok=True)
    steam_running = steam_running or steam_client_running()
    atomic_write(manifest, json.dumps({"accounts": targets, "context": context, "assets": {key: value for key, value in new_assets.items() if key in targets}, "appids": appids}))
    if not steam_running: return ""
    return "Steam shortcuts updated. Restart Steam to refresh the library. If Steam was running, it may overwrite these changes; save again with Steam closed if they do not appear."
