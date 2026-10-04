"""Profile-owned launch tracking; never stop processes by name or Wine prefix."""

import json
import os
from pathlib import Path
import signal
from uuid import UUID, uuid4

from .common import BackendError

MARKER = "FOREST_LAUNCH_TOKEN"
# Shared Wine services are not game processes. Killing them can disrupt another game.
BROKERS = {"wineserver", "wineserver64", "services.exe", "winedevice.exe", "explorer.exe", "rpcss.exe", "plugplay.exe", "svchost.exe", "steam", "steamwebhelper"}


def process_snapshot(pid):
    try:
        directory = Path("/proc") / str(pid)
        if directory.stat().st_uid != os.getuid():
            return None
        fields = (directory / "stat").read_text().rsplit(")", 1)[1].split()
        if fields[0] == "Z":
            return None
        try:
            name = Path(os.readlink(directory / "exe")).name.casefold()
        except OSError:
            name = (directory / "comm").read_text().strip().casefold()
        comm = (directory / "comm").read_text().strip().casefold()
        if comm in BROKERS:
            name = comm
        environment = (directory / "environ").read_bytes().split(b"\0")
        token = next((entry.split(b"=", 1)[1].decode("ascii") for entry in environment
                      if entry.startswith((MARKER + "=").encode())), "")
        return {"pid": pid, "parent": int(fields[1]), "session": int(fields[3]),
                "start": int(fields[19]), "token": token, "name": name}
    except (OSError, ValueError, IndexError, UnicodeError):
        return None


def snapshot_all():
    return {int(path.name): snapshot for path in Path("/proc").iterdir()
            if path.name.isdecimal() and (snapshot := process_snapshot(int(path.name))) is not None}


def prepare_tracking(paths, game_id):
    directory = paths.state / "running"
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    token = str(uuid4())
    path = directory / (token + ".json")
    record = {"game_id": game_id, "token": token, "pid": 0, "start": 0}
    with path.open("x", encoding="utf-8") as output:
        path.chmod(0o600)
        json.dump(record, output)
    return path, record


def finish_tracking(path, record, pid):
    record["pid"] = pid
    state = process_snapshot(pid)
    record["start"] = state["start"] if state and state["token"] == record["token"] else 0
    temporary = path.with_suffix(".tmp")
    with temporary.open("x", encoding="utf-8") as output:
        temporary.chmod(0o600)
        json.dump(record, output)
    temporary.replace(path)


def records(paths):
    directory = paths.state / "running"
    if not directory.is_dir():
        return []
    result = []
    for path in directory.glob("*.json"):
        try:
            if path.is_symlink() or path.stat().st_uid != os.getuid():
                continue
            record = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(record, dict):
                continue
            if (record.get("token") != path.stem or UUID(record["token"]).version != 4
                    or not isinstance(record.get("game_id"), str)
                    or type(record.get("pid")) is not int or type(record.get("start")) is not int):
                continue
            result.append((path, record))
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return result


def owned_processes(record, snapshots):
    leader = snapshots.get(record["pid"])
    valid_leader = leader and record["start"] > 0 and leader["start"] == record["start"]
    selected = {pid for pid, state in snapshots.items()
                if state["token"] == record["token"] and state["name"] not in BROKERS}
    if valid_leader and leader["name"] not in BROKERS:
        selected.add(leader["pid"])
        while True:
            children = {pid for pid, state in snapshots.items()
                        if state["parent"] in selected and state["name"] not in BROKERS
                        and state["token"] in ("", record["token"])}
            if children <= selected:
                break
            selected.update(children)
    return {pid: snapshots[pid] for pid in selected
            if pid not in (0, 1, os.getpid(), os.getppid()) and snapshots[pid]["name"] not in BROKERS}


def running_games(paths):
    entries = records(paths)
    if not entries:
        return []
    snapshots = snapshot_all()
    return sorted({record["game_id"] for _, record in entries if owned_processes(record, snapshots)})


def stop_game(paths, game):
    if game["kind"] == "steam":
        raise BackendError("Steam library games are launched separately and cannot be safely stopped by Forest.", "untracked_game")
    entries = [(path, record) for path, record in records(paths) if record["game_id"] == game["id"]]
    snapshots = snapshot_all() if entries else {}
    targets = {}
    for _, record in entries:
        targets.update(owned_processes(record, snapshots))
    if not targets:
        raise BackendError("This game has no running processes tracked by Forest.", "game_not_running")
    killed = []
    for pid, original in sorted(targets.items(), reverse=True):
        descriptor = None
        try:
            # pidfds prevent a recycled PID from receiving the signal.
            if hasattr(os, "pidfd_open"):
                descriptor = os.pidfd_open(pid)
            current = process_snapshot(pid)
            if not current or current["start"] != original["start"]:
                continue
            if descriptor is not None:
                signal.pidfd_send_signal(descriptor, signal.SIGKILL)
            else:
                raise BackendError("Safe force-stop requires Linux pidfd support.", "unsupported_stop")
            killed.append(pid)
        except ProcessLookupError:
            continue
        except OSError as error:
            raise BackendError(f"Could not force-stop the selected game's process: {error}", "stop_failed") from error
        finally:
            if descriptor is not None:
                os.close(descriptor)
    return {"killed": killed}
