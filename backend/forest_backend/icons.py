"""Read embedded PE icons without executing binaries or importing third-party code."""
import os
from pathlib import Path
import stat
import struct

from .common import BackendError

MAX_BYTES = 8 * 1024 * 1024
MAX_ENTRIES = 4096


class PEIcons:
    def __init__(self, stream):
        self.stream = stream
        self.size = os.fstat(stream.fileno()).st_size
        self.budget = 16 * 1024 * 1024
        self.sections = []
        self.entries = 0

    def read(self, offset, length):
        if offset < 0 or length < 0 or offset + length > self.size or length > self.budget:
            raise ValueError("Invalid or oversized PE resource")
        self.budget -= length
        self.stream.seek(offset)
        result = self.stream.read(length)
        if len(result) != length:
            raise ValueError("Truncated executable")
        return result

    def rva(self, address, length):
        for virtual, raw, size in self.sections:
            if virtual <= address and address + length <= virtual + size:
                return raw + address - virtual
        raise ValueError("Resource address is outside file-backed sections")

    def directory(self, relative, depth=0, visited=None):
        if depth > 2 or relative + 16 > self.resource_size:
            raise ValueError("Invalid resource directory")
        visited = set() if visited is None else visited
        if relative in visited:
            raise ValueError("Cyclic resource directory")
        visited = visited | {relative}
        header = self.read(self.rva(self.resource_rva + relative, 16), 16)
        count = sum(struct.unpack_from("<HH", header, 12))
        self.entries += count
        if self.entries > MAX_ENTRIES or relative + 16 + count * 8 > self.resource_size:
            raise ValueError("Too many resource entries")
        data = self.read(self.rva(self.resource_rva + relative + 16, count * 8), count * 8)
        result = []
        for index in range(count):
            name, pointer = struct.unpack_from("<II", data, index * 8)
            # Names are identifiers only; do not interpret or load arbitrary strings.
            key = name
            position = pointer & 0x7fffffff
            if pointer & 0x80000000:
                if depth == 0 and name not in (3, 14):
                    continue
                result.append((key, self.directory(position, depth + 1, visited)))
            else:
                if position + 16 > self.resource_size:
                    raise ValueError("Invalid resource data entry")
                address, length = struct.unpack_from("<II", self.read(self.rva(self.resource_rva + position, 16), 16))
                if length > MAX_BYTES:
                    raise ValueError("Oversized icon")
                result.append((key, (address, length)))
        return result

    def extract(self):
        if self.read(0, 2) != b"MZ":
            raise ValueError("Not a Windows PE executable")
        pe = struct.unpack("<I", self.read(0x3c, 4))[0]
        header = self.read(pe, 24)
        if header[:4] != b"PE\0\0":
            raise ValueError("Invalid PE signature")
        count, optional_size = struct.unpack_from("<H", header, 6)[0], struct.unpack_from("<H", header, 20)[0]
        if not 0 < count <= 96 or not 120 <= optional_size <= 4096:
            raise ValueError("Invalid PE headers")
        optional = self.read(pe + 24, optional_size)
        magic = struct.unpack_from("<H", optional)[0]
        directory = 96 if magic == 0x10b else 112 if magic == 0x20b else -1
        if directory < 0 or len(optional) < directory + 24 or struct.unpack_from("<I", optional, directory - 4)[0] < 3:
            raise ValueError("No PE resource directory")
        self.resource_rva, self.resource_size = struct.unpack_from("<II", optional, directory + 16)
        if not self.resource_rva or not 16 <= self.resource_size <= 64 * 1024 * 1024:
            raise ValueError("No usable resource directory")
        sections = self.read(pe + 24 + optional_size, count * 40)
        for index in range(count):
            virtual = struct.unpack_from("<I", sections, index * 40 + 12)[0]
            size, raw = struct.unpack_from("<II", sections, index * 40 + 16)
            if raw + size > self.size:
                raise ValueError("Invalid section extent")
            self.sections.append((virtual, raw, size))
        resources = dict(self.directory(0))
        icons = {}
        for identity, languages in resources.get(3, []):
            if isinstance(languages, list):
                icons[identity] = dict(languages)
        for _, languages in resources.get(14, []):
            if not isinstance(languages, list):
                continue
            for language, entry in languages:
                if not isinstance(entry, tuple):
                    continue
                group = self.read(self.rva(*entry), entry[1])
                if len(group) < 6:
                    raise ValueError("Truncated icon group")
                reserved, kind, count = struct.unpack_from("<HHH", group)
                if reserved or kind != 1 or not 0 < count <= 128 or len(group) != 6 + count * 14:
                    raise ValueError("Invalid icon group")
                payloads, table = [], bytearray()
                position = 6 + count * 16
                for index in range(count):
                    definition = group[6 + index * 14:20 + index * 14]
                    size, identity = struct.unpack_from("<IH", definition, 8)
                    choices = icons.get(identity, {})
                    image = choices.get(language) or next(iter(choices.values()), None)
                    if not isinstance(image, tuple) or size != image[1] or not size:
                        raise ValueError("Missing or inconsistent icon image")
                    payload = self.read(self.rva(*image), size)
                    if not (payload.startswith(b"\x89PNG\r\n\x1a\n") or
                            (len(payload) >= 12 and struct.unpack_from("<I", payload)[0] in (12, 40, 108, 124))):
                        raise ValueError("Unsupported embedded icon image")
                    table.extend(definition[:12] + struct.pack("<I", position))
                    position += size
                    if position > MAX_BYTES:
                        raise ValueError("Oversized icon group")
                    payloads.append(payload)
                return struct.pack("<HHH", 0, 1, count) + table + b"".join(payloads)
        raise ValueError("Executable has no embedded icon")


def extract_icon(filename):
    if not isinstance(filename, str) or not filename or "\0" in filename or len(filename) > 8192:
        raise BackendError("Choose an executable to extract its icon.", "icon_extraction_failed")
    descriptor = None
    try:
        descriptor = os.open(Path(filename).expanduser(), os.O_RDONLY | os.O_NONBLOCK)
        if not stat.S_ISREG(os.fstat(descriptor).st_mode):
            raise ValueError("Executable is not a regular file")
        with os.fdopen(descriptor, "rb") as stream:
            descriptor = None
            return PEIcons(stream).extract()
    except (OSError, ValueError, struct.error, OverflowError, TypeError) as error:
        raise BackendError(f"Icon extraction failed: {error}", "icon_extraction_failed") from None
    finally:
        if descriptor is not None:
            os.close(descriptor)
