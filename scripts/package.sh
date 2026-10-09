#!/usr/bin/env bash
# Package the shared baseline binary in an isolated target distro container.
# No C++ compilation here: target-distro Qt must not raise the release baseline.
set -euo pipefail
export LC_ALL=C.UTF-8 PYTHONDONTWRITEBYTECODE=1
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:?Usage: package.sh arch|deb|fedora44 BASELINE_STAGE}"
source_stage="$(realpath "${2:?Supply the stage produced by build-native.sh}")"
case "$target" in arch|deb|fedora44) ;; *) exit 2 ;; esac
[[ "$(uname -m)" == x86_64 ]] || { echo "Release packages require x86_64." >&2; exit 1; }
version="$(python3 "$root/scripts/release.py" version)"
work="$root/build/package-$target"
output="$root/build/release-output"
[[ ! -e "$work" ]] || { echo "Use a fresh packaging directory: $work" >&2; exit 1; }
python3 "$root/scripts/native.py" verify --binary "$source_stage/usr/bin/forest-launcher"
qt_min="$(python3 "$root/scripts/native.py" minimum --library qt)"
glibc_min="$(python3 "$root/scripts/native.py" minimum --library glibc)"
gcc_min="$(python3 "$root/scripts/native.py" minimum --library gcc)"
mkdir -p "$work/stage" "$output"
cp -a "$source_stage/usr" "$work/stage/"
# Run away from the source backend and require the installed copy explicitly.
(
    cd "$work"
    QT_QPA_PLATFORM=offscreen timeout 20s "$work/stage/usr/bin/forest-launcher" --version | grep -Fx "forest-launcher $version"
    QT_QPA_PLATFORM=offscreen timeout 20s "$work/stage/usr/bin/forest-launcher" \
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
depends=('qt6-base>=$qt_min' 'glibc>=$glibc_min' 'gcc-libs>=$gcc_min' 'libglvnd' 'python>=3.11')
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
        python3 "$root/scripts/native.py" verify --binary "$work/extracted/usr/bin/forest-launcher"
        QT_QPA_PLATFORM=offscreen timeout 20s "$work/extracted/usr/bin/forest-launcher" \
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
Depends: python3 (>= 3.11), qt6-qpa-plugins (>= $qt_min), $shlibs
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
        python3 "$root/scripts/native.py" verify --binary "$work/extracted/usr/bin/forest-launcher"
        QT_QPA_PLATFORM=offscreen timeout 20s "$work/extracted/usr/bin/forest-launcher" \
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
Requires: qt6-qtbase-gui >= $qt_min
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
/usr/share/forest-launcher/CHANGELOG.md
/usr/share/applications/io.github.Benaeo.forest-launcher.desktop
%license /usr/share/forest-launcher/LICENSE
EOF
        rpmbuild -bb --define "_topdir $work/rpm" --define 'dist .fc44' \
            --define '_build_id_links none' "$work/rpm/SPECS/forest-launcher.spec"
        package="$work/rpm/RPMS/x86_64/forest-launcher-$version-1.fc44.x86_64.rpm"
        rpm -qp --requires "$package"
        mkdir "$work/extracted"
        (cd "$work/extracted"; rpm2cpio "$package" | cpio -idm --quiet)
        python3 "$root/scripts/native.py" verify --binary "$work/extracted/usr/bin/forest-launcher"
        QT_QPA_PLATFORM=offscreen timeout 20s "$work/extracted/usr/bin/forest-launcher" \
            --backend-dir "$work/extracted/usr/share/forest-launcher/backend" --smoke-test
        # RPM Release remains required metadata, not part of the download name.
        cp "$package" "$output/forest-launcher-$version.fc44.x86_64.rpm"
        ;;
esac
