#!/usr/bin/env bash
# Build the preFlight->WASM module.
# Usage: bash scripts/build-wasm.sh [-clean]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PREFLIGHT="${ROOT}/../preflight"
PATCH_FILE="${ROOT}/patches/preflight-wasm.patch"
BUILD_DIR="${ROOT}/build-wasm"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        -clean|--clean) CLEAN=1 ;;
    esac
done

# 0) Use the portable cmake if it was bootstrapped into deps/
if [[ -d "${ROOT}/deps/cmake-3.30.5-linux-x86_64/bin" ]]; then
    export PATH="${ROOT}/deps/cmake-3.30.5-linux-x86_64/bin:${PATH}"
fi

# 1) Activate emsdk (repo-local first, then /opt)
if [[ -f "${ROOT}/wasm/toolchain/emsdk.env" ]]; then
    # shellcheck disable=SC1091
    source "${ROOT}/wasm/toolchain/emsdk.env"
elif [[ -f "${ROOT}/deps/emsdk/emsdk_env.sh" ]]; then
    # shellcheck disable=SC1091
    source "${ROOT}/deps/emsdk/emsdk_env.sh" >/dev/null
elif [[ -f /opt/emsdk/emsdk_env.sh ]]; then
    # shellcheck disable=SC1091
    source /opt/emsdk/emsdk_env.sh >/dev/null
fi

if ! command -v emcc >/dev/null 2>&1; then
    echo "ERROR: emcc not found. Run ./scripts/setup.sh first." >&2
    exit 1
fi

if [[ ! -d "${PREFLIGHT}" ]]; then
    echo "ERROR: preflight source tree not found at ${PREFLIGHT}" >&2
    echo "Clone https://github.com/oozebot/preFlight.git into ${PREFLIGHT}." >&2
    exit 1
fi

# 1.5) Drop the hand-written CMakeLists.txt files into the vendored
# upstreams that don't ship a usable one. See vendor-cmake/README.md.
if [[ -d "${ROOT}/deps/expat-2.6.3" && -f "${ROOT}/vendor-cmake/expat-2.6.3-CMakeLists.txt" ]]; then
    cp "${ROOT}/vendor-cmake/expat-2.6.3-CMakeLists.txt" \
       "${ROOT}/deps/expat-2.6.3/CMakeLists.txt"
fi
if [[ -d "${ROOT}/deps/heatshrink-src" && -f "${ROOT}/vendor-cmake/heatshrink-CMakeLists.txt" ]]; then
    cp "${ROOT}/vendor-cmake/heatshrink-CMakeLists.txt" \
       "${ROOT}/deps/heatshrink-src/CMakeLists.txt"
fi

# 2) Apply the WASM patch to preflight (idempotent)
if [[ -f "${PATCH_FILE}" ]]; then
    pushd "${PREFLIGHT}" >/dev/null
    if git apply --reverse --check "${PATCH_FILE}" >/dev/null 2>&1; then
        echo "INFO: preflight WASM patch already applied"
    elif git apply --check "${PATCH_FILE}" >/dev/null 2>&1; then
        git apply "${PATCH_FILE}"
        echo "INFO: applied preflight WASM patch"
    else
        echo "WARN: preflight WASM patch did not apply cleanly; continuing" >&2
    fi
    popd >/dev/null
fi

# 3) Configure & build
if [[ ${CLEAN} -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

# Use ccache if present — protects against full-file recompiles when an edit
# is reverted or when emcc is invoked with an identical command line.
CMAKE_EXTRA=()
if [[ -x "${ROOT}/deps/ccache/ccache" ]]; then
    export PATH="${ROOT}/deps/ccache:${PATH}"
    export CCACHE_DIR="${ROOT}/deps/ccache-cache"
    CMAKE_EXTRA+=(
        -DCMAKE_C_COMPILER_LAUNCHER="${ROOT}/deps/ccache/ccache"
        -DCMAKE_CXX_COMPILER_LAUNCHER="${ROOT}/deps/ccache/ccache"
    )
fi

emcmake cmake -S "${ROOT}/wasm" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${CMAKE_EXTRA[@]}"
# NPROC defaults to 2 to avoid overwhelming the host; override with e.g. NPROC=8.
JOBS="${NPROC:-2}"
cmake --build "${BUILD_DIR}" -j"${JOBS}"

if [[ -f "${BUILD_DIR}/slicer.js" && -f "${BUILD_DIR}/slicer.wasm" ]]; then
    echo "OK: artifacts at ${BUILD_DIR}/slicer.js + slicer.wasm"
else
    echo "ERROR: build did not produce slicer.{js,wasm}" >&2
    exit 1
fi
