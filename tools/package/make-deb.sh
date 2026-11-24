#!/bin/sh
# Builds a .deb from the installed tree: the program in /opt/perlin, `perlin` on the PATH, the
# desktop entry and icons. The package manager updates it, so the built-in updater stays off
# (package-managed.txt beside the program tells it so).
#
#   make-deb.sh <dist> <out.deb> <version> <amd64|arm64>
set -eu
dist=$1
out=$2
version=$3
arch=$4
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
pkg="$work/perlin"

mkdir -p "$pkg/DEBIAN" "$pkg/opt/perlin" "$pkg/usr/bin" "$pkg/usr/share/applications" "$pkg/usr/share/icons/hicolor"
cp -a "$dist/." "$pkg/opt/perlin/"
rm -rf "$pkg/opt/perlin/share"
rm -f "$pkg/opt/perlin/portable.txt"
echo "Installed by the perlin package; updates come from the package manager." > "$pkg/opt/perlin/package-managed.txt"
chmod 755 "$pkg/opt/perlin/Perlin"
ln -s /opt/perlin/Perlin "$pkg/usr/bin/perlin"
cp "$root/packaging/linux/io.perlin.client.desktop" "$pkg/usr/share/applications/"
cp -a "$root/packaging/linux/icons/." "$pkg/usr/share/icons/hicolor/"

size=$(du -sk "$pkg" | cut -f1)
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: perlin
Version: $version
Section: games
Priority: optional
Architecture: $arch
Installed-Size: $size
Depends: libc6, libgl1, libasound2t64 | libasound2, libpulse0, libx11-6, libxkbcommon0, libwayland-client0
Maintainer: The Perlin Project <noreply@github.com>
Homepage: https://github.com/${GITHUB_REPOSITORY:-theperlinproject/perlin}
Description: Perlin, a Minecraft: Bedrock Edition compatible client
 A free, open client for Bedrock Edition servers.
CONTROL
cat > "$pkg/DEBIAN/postinst" <<'POST'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database -q /usr/share/applications || true
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
POST
chmod 755 "$pkg/DEBIAN/postinst"
find "$pkg" -type d -exec chmod 755 {} +

dpkg-deb --root-owner-group --build "$pkg" "$out"
rm -rf "$work"
ls -l "$out"
