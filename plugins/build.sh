#!/bin/sh

set -e
here=$(cd "$(dirname "$0")" && pwd)
api=${PERLIN_API:-$here/../build/_deps/perlin_api-src}
[ -f "$api/build.sh" ] || { echo "perlin-api not found at $api: set PERLIN_API to a checkout (https://github.com/theperlinproject/perlin-api)" >&2; exit 1; }
flag=
case "$1" in --wasm32|--wasm64) flag=$1; shift ;; esac
src=${1:?usage: plugins/build.sh [--wasm32|--wasm64] <plugin.cpp> [outdir]}
cd "$here"
exec "$api/build.sh" $flag "$src" "${2:-$here/prebuilt}"
