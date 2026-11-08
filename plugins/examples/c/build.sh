#!/bin/sh
set -e
cd "$(dirname "$0")"
target=wasm64
if [ "$1" = "--wasm32" ]; then
    target=wasm32
    shift
elif [ "$1" = "--wasm64" ]; then
    shift
fi
clang --target=$target -O2 -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,--allow-undefined -Wl,-z,stack-size=65536 \
    -I "${PERLIN_API:?set PERLIN_API to a perlin-api checkout}/sdk" -o "${2:-hello.wasm}" "${1:-hello.c}"
echo "built ${2:-hello.wasm} ($target)"
