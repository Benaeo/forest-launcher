#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
"$root/scripts/build.sh"
exec "${FOREST_BUILD_DIR:-$root/build}/forest-launcher" "$@"
