#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="${ROOT_DIR}/build-switch2-deps"
BUILD_DIR="${ROOT_DIR}/build-switch2"
SDL_SOURCE_DIR="${DEPS_DIR}/sdl-src"
SDL_BUILD_DIR="${DEPS_DIR}/sdl-build"
SDL_PREFIX="${DEPS_DIR}/sdl-prefix"

DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN_FILE="${DEVKITPRO}/libounce/cmake/Switch2.cmake"
OUNCEVK_PREFIX="${DEVKITPRO}/portlibs/switch2"
TARGET="${EDEN_SWITCH2_TARGET:-yuzu-cmd}"
JOBS="${JOBS:-$(nproc)}"

SDL_REPOSITORY="https://github.com/devkitPro/SDL.git"
SDL_REVISION="c329016c0e338ab4397ad930f83330c9fe058348"
SDL_STAMP="eden-switch2-sdl3-v4 ${SDL_REVISION}"

if [[ ! -f "${TOOLCHAIN_FILE}" ]]; then
    echo "ERROR: Switch2.cmake not found at ${TOOLCHAIN_FILE}" >&2
    exit 1
fi

export PATH="${DEVKITPRO}/tools/bin:${DEVKITPRO}/devkitA64/bin:${OUNCEVK_PREFIX}/bin:${DEVKITPRO}/portlibs/switch/bin:${PATH}"
export PKG_CONFIG_PATH="${SDL_PREFIX}/lib/pkgconfig:${OUNCEVK_PREFIX}/lib/pkgconfig:${DEVKITPRO}/portlibs/switch/lib/pkgconfig:${DEVKITPRO}/libounce/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

mkdir -p "${DEPS_DIR}" "${BUILD_DIR}"

if [[ ! -f "${SDL_PREFIX}/.eden-switch2-sdl3" ]] || [[ "$(<"${SDL_PREFIX}/.eden-switch2-sdl3")" != "${SDL_STAMP}" ]]; then
    echo "=== Building Switch 2 SDL3 (devkitPro/SDL @ ${SDL_REVISION:0:10}) ==="
    rm -rf "${SDL_SOURCE_DIR}" "${SDL_BUILD_DIR}" "${SDL_PREFIX}"
    git clone --no-checkout "${SDL_REPOSITORY}" "${SDL_SOURCE_DIR}"
    git -C "${SDL_SOURCE_DIR}" checkout --detach "${SDL_REVISION}"
    # devkitPro SDL's Switch video driver is EGL-only (pulls libouncevk-gl.a / EGLSurface),
    # whereas Eden-NX2 renders directly to OunceVK via vkCreateViSurfaceNN(nwindowGetDefault()).
    # Disable SDL_VIDEO_DRIVER_SWITCH cleanly and use SDL's dummy video driver instead.
    sed -i 's/set(SDL_VIDEO_DRIVER_SWITCH 1)/# disabled on Switch 2 (OunceVK)/' "${SDL_SOURCE_DIR}/CMakeLists.txt"
    sed -i 's|sdl_glob_sources("${SDL3_SOURCE_DIR}/src/video/switch/\*.c")|# disabled on Switch 2 (OunceVK)|' "${SDL_SOURCE_DIR}/CMakeLists.txt"
    sed -i 's/#ifdef SDL_VIDEO_DRIVER_SWITCH/#if 0/' "${SDL_SOURCE_DIR}/src/video/SDL_video.c"
    cmake -S "${SDL_SOURCE_DIR}" -B "${SDL_BUILD_DIR}" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${SDL_PREFIX}" \
        -DCMAKE_PREFIX_PATH="${OUNCEVK_PREFIX}" \
        -DOpenGL_DIR="${OUNCEVK_PREFIX}/lib/cmake/OpenGL" \
        -DSDL_SHARED=OFF \
        -DSDL_OPENGL=OFF \
        -DSDL_OPENGLES=OFF \
        -DSDL_DUMMYVIDEO=ON \
        -DSDL_STATIC=ON \
        -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
        -DSDL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF \
        -DSDL_INSTALL=ON \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF
    cmake --build "${SDL_BUILD_DIR}" --parallel "${JOBS}"
    cmake --install "${SDL_BUILD_DIR}"
    printf '%s' "${SDL_STAMP}" > "${SDL_PREFIX}/.eden-switch2-sdl3"
    rm -rf "${SDL_SOURCE_DIR}" "${SDL_BUILD_DIR}"
fi

echo "=== SDL3 diagnostics ==="
aarch64-none-elf-nm -A "${SDL_PREFIX}/lib/libSDL3.a" 2>/dev/null | grep -i "SWITCH_bootstrap" || echo "SDL3: SWITCH_bootstrap NOT defined in libSDL3.a"
grep -rn "SDL_VIDEO_DRIVER_\(SWITCH\|DUMMY\|OFFSCREEN\)" "${SDL_PREFIX}/include/SDL3/SDL_build_config.h" 2>/dev/null || true
ls "${DEVKITPRO}/portlibs/switch/lib" | grep -i -E "dav1d|libz\.a" || echo "WARN: dav1d/libz not in portlibs/switch/lib"

echo "=== Eden-NX2 Switch 2 Build ==="
echo "CMake: $(cmake --version | head -n1)"
echo "Compiler: $(aarch64-none-elf-g++ --version | head -n1)"
echo "Target: ${TARGET}"

CCACHE_ARGS=()
if [[ "${EDEN_USE_CCACHE:-0}" == "1" ]] && command -v ccache >/dev/null; then
    CCACHE_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    "${CCACHE_ARGS[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${SDL_PREFIX};${OUNCEVK_PREFIX};${DEVKITPRO}/portlibs/switch" \
    -DSDL3_DIR="${SDL_PREFIX}/lib/cmake/SDL3" \
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

cmake --build "${BUILD_DIR}" --target ${TARGET} --parallel "${JOBS}"
echo "=== Built target(s): ${TARGET} ==="

ELF_PATH=""
for candidate in "${BUILD_DIR}/bin/eden-cli.elf" "${BUILD_DIR}/bin/eden-cli" "${BUILD_DIR}/bin/yuzu-cmd" "${BUILD_DIR}/bin/yuzu-cmd.elf"; do
    if [[ -f "${candidate}" ]]; then
        ELF_PATH="${candidate}"
        break
    fi
done

if [[ -n "${ELF_PATH}" ]]; then
    echo "=== Packaging dist/eden-switch2.nro from ${ELF_PATH} ==="
    mkdir -p "${ROOT_DIR}/dist"
    NACP_PATH="${BUILD_DIR}/eden-switch2.nacp"
    NRO_PATH="${ROOT_DIR}/dist/eden-switch2.nro"
    nacptool --create "Eden-NX2" "davidkalil10 (ko-fi.com/davidkalil10)" "0.1.0" "${NACP_PATH}"
    elf2nro "${ELF_PATH}" "${NRO_PATH}" --icon="${ROOT_DIR}/dist/eden_profile.jpg" --nacp="${NACP_PATH}"
    python3 "${ROOT_DIR}/tools/verify_switch2_package.py" "${NRO_PATH}"
fi
