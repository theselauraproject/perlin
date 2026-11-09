#!/bin/sh

set -eu
dist=$1
out=$2
arch=$3
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
app="$work/Perlin.AppDir"

mkdir -p "$app/usr/lib/perlin" "$app/usr/share/applications" "$app/usr/share/icons/hicolor/256x256/apps"

cp -a "$dist/." "$app/usr/lib/perlin/"
rm -rf "$app/usr/lib/perlin/share"
cp -a "$root/packaging/linux/icons/." "$app/usr/share/icons/hicolor/"
cp "$root/packaging/linux/icons/256x256/apps/io.perlin.client.png" "$app/io.perlin.client.png"
cp "$root/packaging/appimage/io.perlin.client.desktop" "$app/io.perlin.client.desktop"
cp "$root/packaging/appimage/io.perlin.client.desktop" "$app/usr/share/applications/"
cp "$root/packaging/appimage/AppRun" "$app/AppRun"
chmod +x "$app/AppRun" "$app/usr/lib/perlin/Perlin"

# a runtime file given here saves appimagetool downloading it from github on every run
runtime_args=""
[ -n "${APPIMAGE_RUNTIME_FILE:-}" ] && [ -s "$APPIMAGE_RUNTIME_FILE" ] && runtime_args="--runtime-file $APPIMAGE_RUNTIME_FILE"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$arch" appimagetool --no-appstream $runtime_args "$app" "$out"
rm -rf "$work"
ls -l "$out"
