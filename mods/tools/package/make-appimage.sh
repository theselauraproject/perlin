#!/bin/sh
# Wraps an installed Linux package (cmake --install ... --prefix dist) into a single-file AppImage.
#   tools/package/make-appimage.sh <dist dir> <output .AppImage> <x86_64|aarch64>
# Needs appimagetool on PATH (CI downloads it); runs it extracted, so no FUSE is needed to build.
set -eu
dist=$1
out=$2
arch=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
app="$work/Perlin.AppDir"

mkdir -p "$app/usr/lib/perlin" "$app/usr/share/applications" "$app/usr/share/icons/hicolor/256x256/apps"
# the game finds resources/ and its other files beside the executable
cp -a "$dist/." "$app/usr/lib/perlin/"
rm -rf "$app/usr/lib/perlin/share"
cp -a "$root/packaging/linux/icons/." "$app/usr/share/icons/hicolor/"
cp "$root/packaging/linux/icons/256x256/apps/io.perlin.client.png" "$app/io.perlin.client.png"
cp "$root/packaging/appimage/io.perlin.client.desktop" "$app/io.perlin.client.desktop"
cp "$root/packaging/appimage/io.perlin.client.desktop" "$app/usr/share/applications/"
cp "$root/packaging/appimage/AppRun" "$app/AppRun"
chmod +x "$app/AppRun" "$app/usr/lib/perlin/Perlin"

APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" appimagetool --no-appstream "$app" "$out"
rm -rf "$work"
ls -l "$out"
