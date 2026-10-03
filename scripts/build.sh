#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build="${FOREST_BUILD_DIR:-$root/build}"

if ! command -v cmake >/dev/null 2>&1; then
    printf 'Error: CMake is required to build Forest Launcher.\n' >&2
    exit 1
fi

generator=()
if command -v ninja >/dev/null 2>&1 && [[ ! -f "$build/CMakeCache.txt" ]]; then
    generator=(-G Ninja)
fi

cmake -S "$root" -B "$build" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$build" --parallel
