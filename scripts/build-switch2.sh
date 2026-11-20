#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-switch2"
DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN_FILE="${DEVKITPRO}/cmake/Switch2.cmake"
TARGET="${EDEN_SWITCH2_TARGET:-common}"

if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo "ERROR: Switch2.cmake not found at ${TOOLCHAIN_FILE}" >&2
    exit 1
fi

export PATH="${DEVKITPRO}/tools/bin:${DEVKITPRO}/devkitA64/bin:${PATH}"
export PKG_CONFIG_PATH="${DEVKITPRO}/portlibs/switch/lib/pkgconfig:${DEVKITPRO}/libnx/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

echo "=== Eden-NX2 Switch 2 Build ==="
echo "CMake: $(cmake --version | head -n1)"
echo "Compiler: $(aarch64-none-elf-g++ --version | head -n1)"
echo "Target: ${TARGET}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNINTENDO_SWITCH2=ON \
    -DCMAKE_C_FLAGS_INIT="-mcpu=cortex-a78c+crypto -O2 -D__SWITCH2__=1 -D__SWITCH__=1" \
    -DCMAKE_CXX_FLAGS_INIT="-mcpu=cortex-a78c+crypto -O2 -D__SWITCH2__=1 -D__SWITCH__=1" \
    -DENABLE_QT=OFF \
    -DENABLE_CUBEB=OFF \
    -DENABLE_LIBUSB=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DENABLE_RESHADE=OFF \
    -DENABLE_WERROR=OFF \
    -DYUZU_ROOM=OFF \
    -DYUZU_ROOM_STANDALONE=OFF \
    -DYUZU_TESTS=OFF \
    -DBUILD_TESTING=OFF \
    -DYUZU_DISABLE_LLVM=ON \
    -DYUZU_STATIC_BUILD=ON

cmake --build "${BUILD_DIR}" --target ${TARGET} --parallel "$(nproc)"
echo "=== Built target(s): ${TARGET} ==="
