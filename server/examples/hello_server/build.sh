#!/bin/sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
sdk=${PERLIN_API_SDK:?set PERLIN_API_SDK to the sdk/ directory of a perlin-api checkout}
target=wasm64
if [ "$1" = "--wasm32" ]; then
    target=wasm32
    shift
elif [ "$1" = "--wasm64" ]; then
    shift
fi
out=${1:-$here/hello_server.wasm}
clang --target=$target -O2 -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -Wl,-z,stack-size=65536 \
  -I "$sdk" -o "$out" "$here/hello_server.c"
echo "built $out ($target)"
