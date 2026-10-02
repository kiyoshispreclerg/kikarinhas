#!/bin/sh
# Builds a .deb from the current tree, on the machine it runs on: the
# dependencies are computed by dpkg-shlibdeps against the libraries
# installed here, so build on the oldest target distro of each family.
# Usage: tools/build-deb.sh [OUTDIR]   (needs build-essential, dpkg-dev,
# the -dev packages from the README, gettext and libgtk2.0-dev).
set -eu

cd "$(dirname "$0")/.."
root=$(pwd)
out=${1:-$root/dist}
version=$(sed -n 's/^VERSION *= *//p' config.mk)
arch=$(dpkg --print-architecture)
maint=${DEB_MAINTAINER:-Kikarinhas contributors <noreply@github.com>}

mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pkg=$work/pkg

make clean
make -j"$(nproc)" PREFIX=/usr
make PREFIX=/usr DESTDIR="$pkg" install
strip --strip-unneeded "$pkg"/usr/bin/kikarinhas*
find "$pkg" -type d -exec chmod 755 {} +

# dpkg-shlibdeps insists on a debian/control in the current directory.
mkdir -p "$work/debian"
printf 'Source: kikarinhas\n\nPackage: kikarinhas\nArchitecture: any\n' > "$work/debian/control"
(
	cd "$work"
	set --
	for b in "$pkg"/usr/bin/kikarinhas*; do set -- "$@" "-e$b"; done
	dpkg-shlibdeps -O "$@" > "$work/substvars"
)
depends=$(sed -n 's/^shlibs:Depends=//p' "$work/substvars")

mkdir -p "$pkg/DEBIAN"
size=$(du -sk "$pkg" | cut -f1)
cat > "$pkg/DEBIAN/control" <<CTL
Package: kikarinhas
Version: $version-1
Architecture: $arch
Maintainer: $maint
Installed-Size: $size
Depends: $depends
Section: video
Priority: optional
Homepage: https://github.com/${GITHUB_REPOSITORY:-kikarinhas/kikarinhas}
Description: chat avatars on a transparent window for OBS
 Shows YouTube and Twitch chat avatars (Stream Avatars format) in an ARGB
 X11 window that OBS can capture. Includes kikarinhas-config, a GTK2
 settings window.
CTL

deb=$out/kikarinhas_${version}-1_${arch}.deb
dpkg-deb --root-owner-group --build "$pkg" "$deb"
echo "$deb"
