#!/usr/bin/env bash
# Build in an isolated distro container; never install dependencies on the host.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:?Usage: package.sh arch|deb|fedora44}"
case "$target" in arch|deb|fedora44) ;; *) exit 2 ;; esac
[[ "$(uname -m)" == x86_64 ]] || { echo "Release packages require x86_64." >&2; exit 1; }
version="$(python3 "$root/scripts/release.py" version)"
work="$root/build/package-$target"
output="$root/build/release-output"
[[ ! -e "$work" ]] || { echo "Use a fresh packaging directory: $work" >&2; exit 1; }
mkdir -p "$work" "$output"
cmake -S "$root" -B "$work/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work/build" --parallel 2
DESTDIR="$work/stage" cmake --install "$work/build"
# Run away from the source backend and require the installed copy explicitly.
(
    cd "$work"
    QT_QPA_PLATFORM=offscreen "$work/stage/usr/bin/forest-launcher" \
        --backend-dir "$work/stage/usr/share/forest-launcher/backend" --smoke-test
)
case "$target" in
    arch)
        cp -a "$work/stage" "$work/pkg-stage"
        cat > "$work/PKGBUILD" <<EOF
pkgname=forest-launcher
pkgver=$version
pkgrel=1
pkgdesc='Native Qt game launcher with a headless Python backend'
arch=('x86_64')
url='https://github.com/Benaeo/forest-launcher'
license=('GPL-3.0-only')
depends=('qt6-base>=6.4' 'python>=3.11')
optdepends=('steam: Steam integration' 'mangohud: performance overlay'
            'lsfg-vk: optional frame generation')
options=('!debug')
package() {
    cp -a "\$startdir/pkg-stage/usr" "\$pkgdir/"
}
EOF
        (cd "$work"; makepkg --nodeps --noconfirm)
        package="$work/forest-launcher-$version-1-x86_64.pkg.tar.zst"
        # Inspect and smoke-test the actual archive, not only its staging tree.
        mkdir "$work/extracted"
        tar -xf "$package" -C "$work/extracted"
        QT_QPA_PLATFORM=offscreen "$work/extracted/usr/bin/forest-launcher" \
            --backend-dir "$work/extracted/usr/share/forest-launcher/backend" --smoke-test
        # pkgrel remains required metadata, not part of the download name.
        cp "$package" "$output/forest-launcher-$version-x86_64.pkg.tar.zst"
        ;;
    deb)
        mkdir -p "$work/debian" "$work/stage/DEBIAN"
        printf 'Source: forest-launcher\nSection: games\nPriority: optional\nMaintainer: Forest Launcher contributors <noreply@github.com>\n\nPackage: forest-launcher\nArchitecture: amd64\nDescription: Native Qt game launcher\n' > "$work/debian/control"
        shlibs="$(cd "$work"; dpkg-shlibdeps -O -e"$work/stage/usr/bin/forest-launcher")"
        shlibs="${shlibs#shlibs:Depends=}"
        shlibs="$(printf '%s' "$shlibs" | python3 "$root/scripts/release.py" deb-dependencies)"
        cat > "$work/stage/DEBIAN/control" <<EOF
Package: forest-launcher
Version: $version
Section: games
Priority: optional
Architecture: amd64
Maintainer: Forest Launcher contributors <noreply@github.com>
Depends: python3 (>= 3.11), $shlibs
Suggests: steam-installer, mangohud, lsfg-vk
Homepage: https://github.com/Benaeo/forest-launcher
Description: Native Qt game launcher for Linux
 Manage native games and Windows games through Proton with a headless
 standard-library Python backend.
EOF
        package="$output/forest-launcher_${version}_amd64.deb"
        dpkg-deb --root-owner-group --build "$work/stage" "$package"
        dpkg-deb --info "$package"
        dpkg-deb --extract "$package" "$work/extracted"
        QT_QPA_PLATFORM=offscreen "$work/extracted/usr/bin/forest-launcher" \
            --backend-dir "$work/extracted/usr/share/forest-launcher/backend" --smoke-test
        ;;
    fedora44)
        mkdir -p "$work/rpm"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
        cat > "$work/rpm/SPECS/forest-launcher.spec" <<EOF
Name: forest-launcher
Version: $version
Release: 1%{?dist}
Summary: Native Qt game launcher for Linux
License: GPL-3.0-only
URL: https://github.com/Benaeo/forest-launcher
Requires: python3 >= 3.11
Requires: qt6-qtbase-gui >= 6.4
Suggests: steam
Suggests: mangohud
Suggests: lsfg-vk
%description
Manage native games and Windows games through Proton with a headless
standard-library Python backend.
%install
mkdir -p %{buildroot}
cp -a $work/stage/usr %{buildroot}/
%files
/usr/bin/forest-launcher
/usr/share/forest-launcher/backend
/usr/share/applications/io.github.Benaeo.forest-launcher.desktop
%license /usr/share/forest-launcher/LICENSE
EOF
        rpmbuild -bb --define "_topdir $work/rpm" --define 'dist .fc44' \
            --define '_build_id_links none' "$work/rpm/SPECS/forest-launcher.spec"
        package="$work/rpm/RPMS/x86_64/forest-launcher-$version-1.fc44.x86_64.rpm"
        rpm -qp --requires "$package"
        mkdir "$work/extracted"
        (cd "$work/extracted"; rpm2cpio "$package" | cpio -idm --quiet)
        QT_QPA_PLATFORM=offscreen "$work/extracted/usr/bin/forest-launcher" \
            --backend-dir "$work/extracted/usr/share/forest-launcher/backend" --smoke-test
        # RPM Release remains required metadata, not part of the download name.
        cp "$package" "$output/forest-launcher-$version.fc44.x86_64.rpm"
        ;;
esac
