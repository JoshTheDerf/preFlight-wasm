cmake_minimum_required(VERSION 3.22)
project(slicer_wasm)

# Release by default
set(CMAKE_BUILD_TYPE Release CACHE STRING "")

# --- Make our custom find-modules visible (Boost/TBB/OpenSSL shims) ---
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# --- Threading model ---
# For the MVP, build single-threaded to avoid SharedArrayBuffer / shared-memory constraints in browsers
# and to simplify linking (no atomics/bulk-memory requirements).
set(EM_PTHREAD_FLAGS "")

# --- Point to locally built Boost (headers + static libs) ---
set(BOOST_PREFIX "${CMAKE_SOURCE_DIR}/../deps/boost-wasm/install")
set(BOOST_INC    "${BOOST_PREFIX}/include")
set(BOOST_LIB    "${BOOST_PREFIX}/lib")

# Export to subprojects (orca)
set(BOOST_ROOT         "${BOOST_PREFIX}" CACHE PATH "Boost root"             FORCE)
set(Boost_ROOT         "${BOOST_PREFIX}" CACHE PATH "Boost root (CamelCase)" FORCE)
set(BOOST_INCLUDEDIR   "${BOOST_INC}"    CACHE PATH "Boost include dir"      FORCE)
set(BOOST_LIBRARYDIR   "${BOOST_LIB}"    CACHE PATH "Boost library dir"      FORCE)
set(Boost_NO_SYSTEM_PATHS ON             CACHE BOOL ""                       FORCE)
set(Boost_USE_STATIC_LIBS ON             CACHE BOOL ""                       FORCE)
set(Boost_USE_MULTITHREADED ON           CACHE BOOL ""                       FORCE)

# --- Staged GMP/MPFR/CGAL toolchain for WASM ---
set(WASM_MATH_PREFIX "${CMAKE_SOURCE_DIR}/../deps/toolchain-wasm/install")
set(WASM_MATH_PREFIX "${WASM_MATH_PREFIX}" CACHE PATH "Prefix with GMP/MPFR/CGAL for WASM" FORCE)
set(ENV{WASM_MATH_PREFIX} "${WASM_MATH_PREFIX}")
list(PREPEND CMAKE_PREFIX_PATH "${WASM_MATH_PREFIX}")

# In-tree header-only deps fetched by scripts/setup.sh
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/../deps/nlohmann_json")

# --- Create WASM shims for complex dependencies ---
add_library(wasm_shims INTERFACE)
target_include_directories(wasm_shims INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims
)
target_compile_definitions(wasm_shims INTERFACE
  WASM_BUILD=1
  TBB_MOCK=1
  SLIC3R_NO_TBB=1
)

# --- Prefer our shims for cereal/libigl but keep real Boost visible ---
include_directories(BEFORE
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims/boost_runtime
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims/libslic3r
  ${BOOST_INC}
)

# --- Eigen include (try common in-tree layouts) ---
# Use the dir that contains the "Eigen/" folder.
set(_eigen_candidates)

if(EIGEN3_INCLUDE_DIR)
  list(APPEND _eigen_candidates "${EIGEN3_INCLUDE_DIR}")
endif()

list(APPEND _eigen_candidates
  "${PROJECT_SOURCE_DIR}/../deps/eigen"
  "${PROJECT_SOURCE_DIR}/deps_src/eigen3"
  "${PROJECT_SOURCE_DIR}/deps_src/eigen"
  "${PROJECT_SOURCE_DIR}/../../preflight/deps_src/eigen3"
  "${PROJECT_SOURCE_DIR}/../../preflight/deps_src/eigen"
)

set(_eigen_resolved "")
foreach(_candidate IN LISTS _eigen_candidates)
  if(EXISTS "${_candidate}/Eigen/Core")
    set(_eigen_resolved "${_candidate}")
    break()
  endif()
endforeach()

if(_eigen_resolved)
  include_directories("${_eigen_resolved}")
  set(EIGEN3_INCLUDE_DIR "${_eigen_resolved}" CACHE PATH "Eigen include dir for WASM build" FORCE)
  set(EIGEN3_INCLUDE_DIRS "${_eigen_resolved}" CACHE PATH "Eigen include dirs for WASM build" FORCE)
else()
  message(WARNING "Eigen headers not found – CGAL components may fail to configure")
endif()

# Eigen runtime-sane flags for wasm
add_compile_definitions(
  EIGEN_DONT_ALIGN_STATICALLY=1
  EIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT=1
  EIGEN_HAS_STD_RESULT_OF=0
  _LIBCPP_ENABLE_CXX17_REMOVED_FEATURES=1
  _LIBCPP_DISABLE_DEPRECATION_WARNINGS=1
)

# --- Disable TBB usage (shim will satisfy REQUIRED without linking) ---
set(SLIC3R_TBB OFF CACHE BOOL "Disable TBB in WASM build" FORCE)
set(SLIC3R_USE_TBB OFF CACHE BOOL "" FORCE)

# OCCT-based functionality is not available in the WASM toolchain
set(SLIC3R_WITH_OCCT OFF CACHE BOOL "Disable OCCT for WASM" FORCE)
set(SLIC3R_WITH_OPENCV OFF CACHE BOOL "Disable OpenCV for WASM" FORCE)

# --- Tell preFlight to avoid GUI and NLS (no Boost.Locale required) ---
set(SLIC3R_GUI OFF CACHE BOOL "Disable GUI for WASM" FORCE)
set(SLIC3R_NLS OFF CACHE BOOL "Disable translations for WASM" FORCE)
set(SLIC3R_ENC_CHECK OFF CACHE BOOL "Disable encoding check for WASM" FORCE)

# Disable preFlight's embedded Python preprocessor (would need a wasm CPython).
set(SLIC3R_PYTHON_PREPROCESSOR OFF CACHE BOOL "Disable Python preprocessor for WASM" FORCE)

# WASM build is fully static; this also makes preflight define
# BOOST_LOG_STATIC_LINK (matching our statically-built libboost_log) instead
# of BOOST_LOG_DYN_LINK which would request shared-library symbol mangling.
set(SLIC3R_STATIC ON CACHE BOOL "Force static linkage for WASM" FORCE)
# Disable STEP format (OCCT wrapper is unavailable for WASM).
set(SLIC3R_ENABLE_FORMAT_STEP OFF CACHE BOOL "Disable STEP format for WASM" FORCE)
# Disable LTO (the LTO path uses host clang and breaks under emcc cross builds).
set(SLIC3R_LTO OFF CACHE BOOL "Disable LTO for WASM" FORCE)

# --- Emscripten-specific compile options ---
if(EMSCRIPTEN)
  # Silence unknown warning option spam
  add_compile_options(-Wno-unknown-warning-option)

  # Pull in Emscripten's built-in zlib port (LibBGCode and others need <zlib.h>).
  add_compile_options(-sUSE_ZLIB=1)
  add_link_options(-sUSE_ZLIB=1)

  # Boost.Log namespace is selected via BOOST_HAS_THREADS at consumer compile
  # time. We build Boost with `threading=single` (see deps/boost-wasm/
  # build_boost.sh), and consumers must NOT define BOOST_HAS_THREADS so both
  # land in `boost::log::v2s_st`. Leave it undefined here.

  # Remove problematic warning flags that Emscripten doesn't support
  string(REPLACE "-Werror=enum-constexpr-conversion" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REPLACE "-Werror=enum-constexpr-conversion" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

  # Disable PCH for WASM builds to avoid include ordering issues
  set(SLIC3R_PCH OFF CACHE BOOL "Disable PCH for WASM" FORCE)

endif()

# --- Subprojects ---

# Provide Threads::Threads interface target for Emscripten (no real pthread lib needed)
if(NOT TARGET Threads::Threads)
  add_library(Threads::Threads INTERFACE IMPORTED)
endif()

if(NOT EMSCRIPTEN)
  target_compile_options(Threads::Threads INTERFACE -pthread)
  target_link_options(Threads::Threads INTERFACE -pthread)
endif()

# Provide TBB shim targets early so subprojects can link even if find_package(TBB) is disabled
if(EMSCRIPTEN)
  if(NOT TARGET TBB::tbb)
    add_library(TBB::tbb INTERFACE IMPORTED)
    # Point includes to our wasm shims; they provide minimal containers/APIs
    set_target_properties(TBB::tbb PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/wasm_shims;${CMAKE_CURRENT_LIST_DIR}/wasm_shims/tbb;${CMAKE_CURRENT_LIST_DIR}/wasm_shims/oneapi"
    )
  endif()
  if(NOT TARGET TBB::tbbmalloc)
    add_library(TBB::tbbmalloc INTERFACE IMPORTED)
    set_target_properties(TBB::tbbmalloc PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/wasm_shims;${CMAKE_CURRENT_LIST_DIR}/wasm_shims/tbb;${CMAKE_CURRENT_LIST_DIR}/wasm_shims/oneapi"
    )
  endif()
endif()

# Force-resolve nlohmann_json early so the bridge subdir can pick up the target.
find_package(nlohmann_json REQUIRED)

# --- Stub IMPORTED targets for deps that are gated out under Emscripten ---
# These provide empty INTERFACE libraries so call sites that reference them
# (e.g. libnest2d → NLopt::nlopt) still link, even though no native lib exists.
if (EMSCRIPTEN)
    if (NOT TARGET NLopt::nlopt)
        add_library(NLopt::nlopt INTERFACE IMPORTED)
    endif ()
    if (NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB INTERFACE IMPORTED)
    endif ()
    if (NOT TARGET PNG::PNG)
        add_library(PNG::PNG INTERFACE IMPORTED)
    endif ()
    if (NOT TARGET CURL::libcurl)
        add_library(CURL::libcurl INTERFACE IMPORTED)
    endif ()
    if (NOT TARGET OpenGL::GL)
        add_library(OpenGL::GL INTERFACE IMPORTED)
    endif ()
endif ()

# heatshrink (required by LibBGCode); built from a vendored tarball under deps/
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/heatshrink-src ${CMAKE_BINARY_DIR}/heatshrink-build EXCLUDE_FROM_ALL)

# EXPAT — used by 3MF/AMF XML parsing in libslic3r; built from vendored source.
add_subdirectory(${CMAKE_SOURCE_DIR}/../deps/expat-2.6.3 ${CMAKE_BINARY_DIR}/expat-build EXCLUDE_FROM_ALL)

add_subdirectory(../bridge ${CMAKE_BINARY_DIR}/bridge-build)
add_subdirectory(../../preflight   ${CMAKE_BINARY_DIR}/preflight-build  EXCLUDE_FROM_ALL)

# --- Finalise bridge wiring now that targets exist ---
if (TARGET preflight_wasm_bridge)
  if (EXISTS "${PROJECT_SOURCE_DIR}/deps_src/semver/include/semver/semver.h")
    target_include_directories(preflight_wasm_bridge BEFORE PRIVATE
      ${PROJECT_SOURCE_DIR}/deps_src/semver/include
    )
  elseif (EXISTS "${PROJECT_SOURCE_DIR}/deps_src/semver/semver/semver.h")
    target_include_directories(preflight_wasm_bridge BEFORE PRIVATE
      ${PROJECT_SOURCE_DIR}/deps_src/semver
    )
  elseif (EXISTS "${PROJECT_SOURCE_DIR}/../../preflight/deps_src/semver/include/semver/semver.h")
    target_include_directories(preflight_wasm_bridge BEFORE PRIVATE
      ${PROJECT_SOURCE_DIR}/../../preflight/deps_src/semver/include
    )
  elseif (EXISTS "${PROJECT_SOURCE_DIR}/../../preflight/deps_src/semver/semver/semver.h")
    target_include_directories(preflight_wasm_bridge BEFORE PRIVATE
      ${PROJECT_SOURCE_DIR}/../../preflight/deps_src/semver
    )
  endif()

  if(NOT SLIC3R_WITH_OCCT)
    target_compile_definitions(preflight_wasm_bridge PUBLIC SLIC3R_NO_OCCT)
  endif()
endif()

if (NOT SLIC3R_WITH_OCCT)
  if (TARGET libslic3r)
    target_compile_definitions(libslic3r PUBLIC SLIC3R_NO_OCCT)
    target_include_directories(libslic3r BEFORE PUBLIC
      ${PROJECT_SOURCE_DIR}/wasm_shims
      ${PROJECT_SOURCE_DIR}/wasm_shims/boost_runtime
    )
  endif()
  if (TARGET libslic3r_cgal)
    target_compile_definitions(libslic3r_cgal PUBLIC SLIC3R_NO_OCCT)
    target_include_directories(libslic3r_cgal BEFORE PUBLIC
      ${PROJECT_SOURCE_DIR}/wasm_shims
      ${PROJECT_SOURCE_DIR}/wasm_shims/boost_runtime
    )
  endif()
endif()

# Propagate the in-tree nanosvg headers to libslic3r so EmbossShape / CutSurface
# can find `#include "nanosvg/nanosvg.h"` without preflight's deps autobuild.
if (TARGET libslic3r)
  target_include_directories(libslic3r BEFORE PUBLIC
    ${PROJECT_SOURCE_DIR}/../deps/nanosvg
  )
endif ()
if (TARGET libslic3r_cgal)
  target_include_directories(libslic3r_cgal BEFORE PUBLIC
    ${PROJECT_SOURCE_DIR}/../deps/nanosvg
  )
endif ()

# --- Suppress third-party warnings for Emscripten ---
if(EMSCRIPTEN AND TARGET mcut)
  target_compile_options(mcut PRIVATE -Wno-deprecated-non-prototype)
endif()

# Eigen settings for libslic3r
if(EMSCRIPTEN AND TARGET libslic3r)
  target_compile_definitions(libslic3r PRIVATE
    EIGEN_DONT_ALIGN_STATICALLY=1
    EIGEN_DISABLE_UNALIGNED_ARRAY_ASSERT=1
    ORCA_DISABLE_OPENVDB=1
  )
endif()

if(EMSCRIPTEN AND TARGET libslic3r_cgal)
  target_compile_options(libslic3r_cgal PRIVATE
    -frounding-math
    -fno-fast-math
    -ffp-contract=off
  )
  target_compile_definitions(libslic3r_cgal PRIVATE CGAL_DISABLE_ROUNDING_MATH_CHECK)
endif()

# --- Executable (bridge that links to Orca slicer) ---
# The `slicer` executable is just a thin shim that pulls in the already-built
# preflight_wasm_bridge static lib. We deliberately do NOT re-compile
# preflight_wrap.cpp here — that would lose every include directory and
# definition wired up in bridge/CMakeLists.txt.
file(WRITE ${CMAKE_BINARY_DIR}/slicer_main.cpp "// auto-generated WASM entry stub\nint main() { return 0; }\n")
add_executable(slicer ${CMAKE_BINARY_DIR}/slicer_main.cpp)

target_include_directories(slicer PRIVATE
  # Ensure our WASM shims take precedence over system/Boost includes
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims/libslic3r
  ${CMAKE_CURRENT_SOURCE_DIR}/wasm_shims/boost_runtime
  # Project includes
  ../bridge
  ../../preflight/src
  ${CMAKE_BINARY_DIR}/preflight-build/src/libslic3r
  # Finally, real Boost headers if needed
  ${BOOST_INC}
)

if(NOT SLIC3R_WITH_OCCT)
  target_compile_definitions(slicer PRIVATE SLIC3R_NO_OCCT)
endif()

# Link preFlight slicer libraries and our shims. The bridge static lib carries
# the C entry points (orc_init / orc_slice / orc_describe_config); whole-archive
# is required so they aren't dropped during static linking.
target_link_libraries(slicer PRIVATE
  -Wl,--whole-archive
  preflight_wasm_bridge
  -Wl,--no-whole-archive
  libslic3r
  libslic3r_cgal
  wasm_shims
)

# Boost static archives. We wrap them in --start-group/--end-group so the
# linker can resolve circular references between the boost log internals.
target_link_libraries(slicer PRIVATE
  -Wl,--start-group
  "${BOOST_LIB}/libboost_system.a"
  "${BOOST_LIB}/libboost_filesystem.a"
  "${BOOST_LIB}/libboost_regex.a"
  "${BOOST_LIB}/libboost_chrono.a"
  "${BOOST_LIB}/libboost_atomic.a"
  "${BOOST_LIB}/libboost_date_time.a"
  "${BOOST_LIB}/libboost_iostreams.a"
  "${BOOST_LIB}/libboost_program_options.a"
  "${BOOST_LIB}/libboost_log.a"
  "${BOOST_LIB}/libboost_log_setup.a"
  -Wl,--end-group
)

# --- Emscripten link flags ---
set(EM_FLAGS
  "-O3"
  "-sMODULARIZE=1"
  "-sEXPORT_ES6=1"
  "-sWASM_BIGINT=1"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sSTACK_SIZE=16777216"
  "-sABORTING_MALLOC=0"
  "-sENVIRONMENT=web,worker"
  "-sDISABLE_EXCEPTION_CATCHING=0"
  "-fexceptions"
  ${EM_PTHREAD_FLAGS}
)


# --- Fix em++ options: use target_link_options instead of LINK_FLAGS ---
# First, clear any previous LINK_FLAGS we set earlier:

# --- Emscripten link options ---
# Note: -O0 at link time. Both -O3 and -O2 trigger wasm-opt passes that
# assert internally on our slicer.wasm (binaryen ArenaVector OOB). The
# per-TU -O3 compile flags above already produce optimized .o files, so the
# missing link-time wasm-opt pass is a manageable loss for now.
target_link_options(slicer PRIVATE
  -O0
  -sALLOW_MEMORY_GROWTH=1
  -sMALLOC=emmalloc
  -sSTACK_SIZE=16777216
  -sINITIAL_MEMORY=268435456
  -sMAXIMUM_MEMORY=4294967296
  -sABORTING_MALLOC=0
  -sENVIRONMENT=web,worker,node
  -sMODULARIZE=1
  -sEXPORT_NAME=PreflightModule
  -sDISABLE_EXCEPTION_CATCHING=0
  -sEMULATE_FUNCTION_POINTER_CASTS=1
  -fexceptions
  -sASSERTIONS=1
  --preload-file=../../preflight/resources@/resources
  "-sEXPORTED_FUNCTIONS=['_orc_init','_orc_slice','_malloc','_free','_orc_free','_orc_decode_exception']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','getValue','setValue','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAP8','HEAPU8','HEAP32','HEAPU32']"
)
