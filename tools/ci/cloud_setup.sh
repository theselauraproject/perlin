#!/usr/bin/env bash
set -euo pipefail

sudo_cmd=""
if [ "$(id -u)" -ne 0 ]; then
    sudo_cmd="sudo"
fi

$sudo_cmd apt-get update
$sudo_cmd apt-get install -y build-essential cmake ninja-build ccache pkg-config python3 libssl-dev libgl-dev \
    libegl-dev libdrm-dev libgbm-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
    libxfixes-dev libxtst-dev libxrender-dev libwayland-dev wayland-protocols libxkbcommon-dev libasound2-dev \
    libpulse-dev libudev-dev libdbus-1-dev mesa-vulkan-drivers xvfb valgrind heaptrack

root="$(cd "$(dirname "$0")/../.." && pwd)"
export CPM_SOURCE_CACHE="${CPM_SOURCE_CACHE:-$HOME/.cache/perlin-cpm}"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/perlin-ccache}"
mkdir -p "$CPM_SOURCE_CACHE" "$CCACHE_DIR"
ccache --max-size=5G

cmake --preset unix-ninja-fast -S "$root" -B "${PERLIN_BUILD_DIR:-$root/build/unix-ninja-fast}"
