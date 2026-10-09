#!/usr/bin/env bash
# Build once on the supported baseline; package this binary without rebuilding it.
set -euo pipefail
export LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$(uname -m)" == x86_64 ]] || { echo "Native releases require x86_64." >&2; exit 1; }
# Keep the compiler, C library and Qt coherent; never downgrade just Qt on Arch.
source /etc/os-release
[[ "$ID" == ubuntu && "${VERSION_ID:-}" == 24.04 ]] || {
    echo "Build native releases in the Ubuntu 24.04 baseline container." >&2; exit 1;
}
qt_min="$(python3 "$root/scripts/native.py" minimum --library qt)"
qt_version="$(qmake6 -query QT_VERSION)"
[[ "$qt_version" == "$qt_min" ]] || {
    echo "Expected baseline Qt $qt_min, found $qt_version. Review before upgrading." >&2; exit 1;
}
qt_libraries="$(qmake6 -query QT_INSTALL_LIBS)"
work="$root/build/native-baseline"
stage="$root/build/native-stage"
[[ ! -e "$work" && ! -e "$stage" ]] || { echo "Use fresh native build/stage directories." >&2; exit 1; }
cmake -S "$root" -B "$work" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DQt6_DIR="$qt_libraries/cmake/Qt6" \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work" --target forest-launcher --parallel 2
DESTDIR="$stage" cmake --install "$work"
python3 "$root/scripts/native.py" verify --binary "$stage/usr/bin/forest-launcher"
version="$(python3 "$root/scripts/release.py" version)"
(
    cd "$work"
    QT_QPA_PLATFORM=offscreen timeout 20s "$stage/usr/bin/forest-launcher" --version | grep -Fx "forest-launcher $version"
    QT_QPA_PLATFORM=offscreen timeout 20s "$stage/usr/bin/forest-launcher" \
        --backend-dir "$stage/usr/share/forest-launcher/backend" --smoke-test
)
tar -C "$stage" -czf "$root/build/native-stage.tar.gz" usr
