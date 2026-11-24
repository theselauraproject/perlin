#!/bin/sh
# Builds a compressed disk image holding Perlin.app and a link to /Applications, the usual
# drag-to-install window. Signs the image when MACOS_SIGNING_IDENTITY is set.
#
#   make-dmg.sh <folder with Perlin.app> <out.dmg>
set -eu
dist=$1
out=$2
app=$(find "$dist" -maxdepth 1 -name '*.app' | head -1)
[ -n "$app" ] || { echo "no .app in $dist" >&2; exit 1; }
work=$(mktemp -d)
cp -R "$app" "$work/"
ln -s /Applications "$work/Applications"
rm -f "$out"
# hdiutil now and then fails with "Resource busy" on CI machines; a few tries get past it.
for i in 1 2 3 4 5; do
  if hdiutil create -volname Perlin -srcfolder "$work" -ov -fs HFS+ -format UDZO "$out"; then break; fi
  [ "$i" = 5 ] && exit 1
  sleep $((i * 5))
done
if [ -n "${MACOS_SIGNING_IDENTITY:-}" ]; then
  codesign --force --timestamp --sign "$MACOS_SIGNING_IDENTITY" "$out"
fi
rm -rf "$work"
ls -l "$out"
