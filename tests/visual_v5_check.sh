#!/bin/sh
set -eu
CXX="${CXX:-c++}"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT HUP INT TERM
for file in shield_item_icon_selftest builtin_camera_presets_selftest creative_catalog_selftest; do
  "$CXX" -std=c++20 -O2 -I"$ROOT/src" "$ROOT/tests/$file.cpp" -o "$TEMP/$file"
  "$TEMP/$file"
done
