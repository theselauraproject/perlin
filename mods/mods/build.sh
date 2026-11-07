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
src="${1:-waila/waila.cpp}"
out="${2:-prebuilt}"
name="$(basename "$src" .cpp)"
srcdir="$(dirname "$src")" # headers next to the source are found with #include <dir/header.hpp>
mkdir -p "$out"
manifest="$(dirname "$src")/$name.json"
gen="$out/gen/$name"
mkdir -p "$gen"
if [ -f "$manifest" ]; then
    python3 tools/perlin_settings.py "$manifest" "$gen/mod_settings.hpp"
    if [ "$(cd "$(dirname "$manifest")" && pwd)" != "$(cd "$out" && pwd)" ]; then cp "$manifest" "$out/$name.json"; fi
fi
exports="-Wl,--export=perlin_alloc -Wl,--export=perlin_free -Wl,--export=perlin_event -Wl,--export=perlin_subscriptions -Wl,--export=perlin_subscriptions_hi"

if [ "$target" = wasm32 ]; then
    sdk="${WASI_SDK_PATH:?set WASI_SDK_PATH to a wasi-sdk install (or drop --wasm32 to build with Emscripten)}"
    "$sdk/bin/clang++" --sysroot="$sdk/share/wasi-sysroot" --target=wasm32-wasip1 -std=c++23 -O2 -fno-exceptions -fno-rtti \
        -mexec-model=reactor -Isdk -I"$srcdir" -I"$gen" -Wall -Wextra "$src" -o "$out/$name.wasm" $exports -Wl,--strip-all
else
    emxx="em++"
    if ! command -v "$emxx" >/dev/null 2>&1; then
        if [ -n "$EMSDK" ] && [ -x "$EMSDK/upstream/emscripten/em++" ]; then
            emxx="$EMSDK/upstream/emscripten/em++"
        else
            echo "em++ not found: activate emsdk (source emsdk_env.sh) or build with --wasm32 and wasi-sdk" >&2
            exit 1
        fi
    fi
    "$emxx" -std=c++23 -O2 -fno-exceptions -fno-rtti -Isdk -I"$srcdir" -I"$gen" -Wall -Wextra "$src" -o "$out/$name.wasm" \
        -sMEMORY64=1 -sSTANDALONE_WASM=1 -sPURE_WASI=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=1MB -sMAXIMUM_MEMORY=1GB -sSTACK_SIZE=256KB \
        -sERROR_ON_UNDEFINED_SYMBOLS=0 --no-entry \
        -sEXPORTED_FUNCTIONS=_perlin_alloc,_perlin_free,_perlin_event,_perlin_subscriptions
fi
echo "$out/$name.wasm ($target)"
