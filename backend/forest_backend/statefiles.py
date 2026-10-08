"""Title-named ownership records keyed internally by stable game identity."""
import os

from .common import BackendError, game_name
from .jsonfiles import private_directory, read_document


def ownership_path(paths, category, game_id, slug):
    directory = paths.state / category
    private_directory(directory)
    # Callers pass a normalized filename, not a user-selected path.
    if game_name(slug) != slug:
        raise BackendError("Invalid ownership record filename.")
    target = directory / (slug + ".json")
    found = None
    for path in directory.glob("*.json"):
        record = read_document(path)
        if path == target and record.get("game_id") != game_id:
            raise BackendError(f"An ownership record already exists for {slug}.")
        if record.get("game_id") == game_id:
            if found is not None:
                raise BackendError("Duplicate shortcut ownership records; nothing was changed.")
            found = path
    if found is not None and found != target:
        os.replace(found, target)
    return target
