#!/bin/sh
# Builds an AppImage from the current tree, on the machine it runs on: build
# on the oldest distro you want it to run on (the bundled libraries come from
# here, and glibc is taken from the host at run time). Bundles kikarinhas and
# kikarinhas-config with their libraries, plus the gdk-pixbuf loaders GTK2
# needs to show images. X11, fontconfig, freetype and the other libraries on
# the AppImage exclude list come from the host.
# Usage: tools/build-appimage.sh [OUTDIR]   (needs the -dev packages from the
# README, gettext, libgtk2.0-dev, curl and file; downloads linuxdeploy.)
set -eu

cd "$(dirname "$0")/.."
root=$(pwd)
out=${1:-$root/dist}
version=$(sed -n 's/^VERSION *= *//p' config.mk)
arch=$(uname -m)

mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appdir=$work/AppDir

make clean
make -j"$(nproc)" PREFIX=/usr
make PREFIX=/usr DESTDIR="$appdir" install
strip --strip-unneeded "$appdir"/usr/bin/kikarinhas*

# gdk-pixbuf loaders, with a cache that AppRun rewrites for the mount point.
moddir=$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0)
query=$(pkg-config --variable=gdk_pixbuf_query_loaders gdk-pixbuf-2.0)
dest=$appdir/usr/lib/gdk-pixbuf-2.0/2.10.0
mkdir -p "$dest/loaders"
cp "$moddir"/*.so "$dest/loaders/"
GDK_PIXBUF_MODULEDIR=$dest/loaders "$query" | sed "s|$appdir|@APPDIR@|g" > "$dest/loaders.cache.in"

cd "$work"
base=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous
curl -fsSL -o linuxdeploy "$base/linuxdeploy-$arch.AppImage"
curl -fsSL -o linuxdeploy-plugin-appimage "https://github.com/linuxdeploy/linuxdeploy-plugin-appimage/releases/download/continuous/linuxdeploy-plugin-appimage-$arch.AppImage"
chmod +x linuxdeploy linuxdeploy-plugin-appimage

libs=
for so in "$dest"/loaders/*.so; do libs="$libs --library $so"; done

# No FUSE on CI runners: run the AppImages extracted.
export APPIMAGE_EXTRACT_AND_RUN=1
export PATH=$work:$PATH
export LINUXDEPLOY_OUTPUT_VERSION=$version
# shellcheck disable=SC2086
./linuxdeploy --appdir "$appdir" \
	--executable "$appdir/usr/bin/kikarinhas" \
	--executable "$appdir/usr/bin/kikarinhas-config" \
	$libs \
	--desktop-file "$root/data/kikarinhas.desktop" \
	--icon-file "$root/data/kikarinhas.svg" \
	--custom-apprun "$root/packaging/AppRun" \
	--output appimage

mv ./*.AppImage "$out/kikarinhas-$version-$arch.AppImage"
echo "$out/kikarinhas-$version-$arch.AppImage"
