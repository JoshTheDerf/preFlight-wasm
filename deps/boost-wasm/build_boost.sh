#!/usr/bin/env bash
# Build Boost for Emscripten (WASM) with pthreads enabled (Emscripten 4.x).
# Output prefix: deps/boost-wasm/install
set -euo pipefail

# -------- config --------
BOOST_VER=1.83.0
NPROC="${NPROC:-$(nproc)}"
# ------------------------

BOOST_DIR="boost_${BOOST_VER//./_}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${SCRIPT_DIR}/install"

say() { printf '%s\n' "$*" ; }

# 0) Ensure Emscripten toolchain is available
if ! command -v emcc >/dev/null 2>&1; then
  if [ -f /opt/emsdk/emsdk_env.sh ]; then
    # shellcheck disable=SC1091
    source /opt/emsdk/emsdk_env.sh
  else
    say "❌ emcc not found and /opt/emsdk/emsdk_env.sh missing. Install/supply emsdk."
    exit 1
  fi
fi

# 1) Fetch Boost
cd "${SCRIPT_DIR}"
if [ ! -d "${BOOST_DIR}" ]; then
  say "⬇️  Downloading Boost ${BOOST_VER} ..."
  curl -L -o "${BOOST_DIR}.tar.gz" "https://archives.boost.io/release/${BOOST_VER}/source/${BOOST_DIR}.tar.gz"
  tar xf "${BOOST_DIR}.tar.gz"
fi

cd "${BOOST_DIR}"

# 2) Bootstrap b2
if [ ! -x ./b2 ]; then
  ./bootstrap.sh
fi

# 3) Configure user-config.jam for emscripten toolset (clang)
mkdir -p tools/build/src
cat > tools/build/src/user-config.jam <<JAM
using clang : emscripten :
  ${EMSDK}/upstream/emscripten/em++ :
  <archiver>"${EMSDK}/upstream/emscripten/emar"
  <ranlib>"${EMSDK}/upstream/emscripten/emranlib" ;
JAM

# 4) Components needed by Orca (omit locale archive; nowide is header-only)
COMPONENTS="--with-system --with-filesystem --with-thread \
            --with-regex --with-chrono --with-atomic --with-date_time \
            --with-iostreams --with-program_options --with-log"

say "🔨 Building Boost components for Emscripten (this can take a while)..."
# Threading=single matches the namespace that Emscripten consumers select
# (v2s_st in Boost.Log) when BOOST_HAS_THREADS isn't defined. Mixing
# threading=multi here with single-threaded consumers gives v2s_mt_posix in
# the lib but v2s_st in consumers → undefined-symbol at link.
./b2 \
  toolset=clang-emscripten \
  link=static runtime-link=static \
  threading=single \
  variant=release \
  cxxflags="-Wno-unused-command-line-argument -DBOOST_LOG_WITHOUT_SYSLOG -DBOOST_LOG_WITHOUT_EVENT_LOG -DBOOST_LOG_WITHOUT_DEBUG_OUTPUT -DBOOST_LOG_WITHOUT_IPC" \
  linkflags="-Wno-unused-command-line-argument" \
  define=BOOST_LOG_WITHOUT_SYSLOG \
  define=BOOST_LOG_WITHOUT_EVENT_LOG \
  define=BOOST_LOG_WITHOUT_DEBUG_OUTPUT \
  define=BOOST_LOG_WITHOUT_IPC \
  ${COMPONENTS} \
  -j"${NPROC}" \
  install --prefix="${PREFIX}"

say "✅ Boost installed to: ${PREFIX}"
say "   Include: ${PREFIX}/include"
say "   Libs:    ${PREFIX}/lib"

# 5) Sanity check: require only the archives we actually built on WASM
need_libs=(
  libboost_system.a
  libboost_filesystem.a
  libboost_thread.a
  libboost_regex.a
  libboost_chrono.a
  libboost_atomic.a
  libboost_date_time.a
  libboost_iostreams.a
  libboost_program_options.a
  libboost_log.a
  libboost_log_setup.a
)

missing=0
for f in "${need_libs[@]}"; do
  if [ ! -f "${PREFIX}/lib/${f}" ]; then
    say "⚠️  Missing ${f}"
    missing=1
  fi
done

if [ "$missing" -eq 1 ]; then
  say "❌ Some Boost archives are missing in ${PREFIX}/lib (see above)."
  say "   You can retry with fewer parallel jobs:  NPROC=2 $0"
  exit 1
fi

say "🎉 Required Boost archives are present. Note: boost.nowide is header-only; boost.locale is omitted."