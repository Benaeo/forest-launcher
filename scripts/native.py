#!/usr/bin/env python3
"""Guard the shared x86-64 native release binary before distro packaging."""
import argparse
import os
from pathlib import Path
import re
import subprocess


# Ubuntu 24.04 supplies this Qt and a GCC 13 / glibc 2.39 build environment.
# Raise these floors deliberately, only after reviewing runtime compatibility.
MINIMUM = {"qt": "6.4.2", "glibc": "2.39", "gcc": "13.2"}
VERSION_LIMITS = {
    "Qt": (6, 4),
    "GLIBC": (2, 39),
    "GLIBCXX": (3, 4, 32),
    "CXXABI": (1, 3, 14),
    "GCC": (4, 2, 0),
}
SONAMES = {
    "libQt6Core.so.6", "libQt6Gui.so.6", "libQt6Widgets.so.6",
    "libGLX.so.0", "libOpenGL.so.0", "libGL.so.1",
    "libstdc++.so.6", "libgcc_s.so.1", "libc.so.6", "libm.so.6",
    "libpthread.so.0", "libdl.so.2", "librt.so.1",
}


def verify(binary):
    binary = Path(binary)
    with binary.open("rb") as stream:
        header = stream.read(20)
    if (header[:6] != b"\x7fELF\x02\x01" or len(header) != 20
            or int.from_bytes(header[18:20], "little") != 62):
        raise ValueError("Native packages require a little-endian x86-64 ELF binary")
    env = dict(os.environ, LC_ALL="C")
    dynamic = subprocess.check_output(["readelf", "--dynamic", "--wide", str(binary)], text=True, env=env)
    needed = set(re.findall(r"\(NEEDED\).*\[([^\]]+)\]", dynamic))
    if not {"libQt6Core.so.6", "libQt6Gui.so.6", "libQt6Widgets.so.6"} <= needed:
        raise ValueError("Native releases must use dynamically linked system Qt Widgets")
    if unexpected := needed - SONAMES:
        raise ValueError(f"Review new cross-distro library dependencies: {sorted(unexpected)}")
    if re.search(r"\((?:RPATH|RUNPATH)\)", dynamic):
        raise ValueError("Installed native binaries must not contain build or bundled-library search paths")
    versions = subprocess.check_output(["readelf", "--version-info", "--wide", str(binary)], text=True, env=env)
    names = set(re.findall(r"Name:\s+(\S+)", versions))
    # The version tag ensures we really compiled against the chosen Qt minor.
    # Do not disable Qt version tagging to disguise a newer build as compatible.
    if "Qt_6.4" not in names:
        raise ValueError("Release binary must retain its Qt_6.4 build-version tag")
    for name in sorted(names):
        match = re.fullmatch(r"(Qt|GLIBC|GLIBCXX|CXXABI|GCC)_(\d+(?:\.\d+)*)", name)
        if match is None:
            raise ValueError(f"Unsupported/private library version requirement: {name}")
        family, number = match.groups()
        required = tuple(map(int, number.split(".")))
        limit = VERSION_LIMITS[family]
        if required > limit:
            raise ValueError(f"{name} exceeds the release baseline {family}_{'.'.join(map(str, limit))}")
    print(f"Verified native binary against the Qt {MINIMUM['qt']} / glibc {MINIMUM['glibc']} / GCC {MINIMUM['gcc']} baseline: {binary}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("minimum", "verify"))
    parser.add_argument("--library", choices=MINIMUM)
    parser.add_argument("--binary", type=Path)
    args = parser.parse_args()
    if args.command == "minimum":
        if not args.library:
            parser.error("--library is required")
        print(MINIMUM[args.library])
    else:
        if not args.binary:
            parser.error("--binary is required")
        verify(args.binary)


if __name__ == "__main__":
    main()
