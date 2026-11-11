#!/bin/sh
# usage: run.sh <slangc> <server build dir with libperlin_server_core.a and libminiz.a> [seed ...]
# compiles the vanilla terrain kernel to c++ with slang, builds it with fused multiply-add off (what `precise` does on a gpu) and compares it with the cpu graph
set -e
slangc="$1"; build="$2"; shift 2
here="$(cd "$(dirname "$0")" && pwd)"
server="$here/../.."
work="$(mktemp -d)"
"$slangc" -target cpp -entry csMain -stage compute -o "$work/kernel.inc" "$server/src/gpu/shaders/terrain_vanilla.slang" 2>&1 | grep -v "precise" | grep -i "error" || true
slanginc="$(dirname "$slangc")/../include"
miniz="$(find "$build" -name 'libminiz*' | head -1)"
cp "$here/harness.cpp" "$work/harness.cpp"
g++ -std=c++23 -O1 -ffp-contract=off -I"$work" -I"$server/src" -I"$slanginc" "$work/harness.cpp" "$build/libperlin_server_core.a" "$miniz" -lcrypto -lpthread -o "$work/harness"
status=0
[ $# -eq 0 ] && set -- 12345
for seed in "$@"; do
    "$work/harness" "$seed" || status=1
done
rm -rf "$work"
exit $status
