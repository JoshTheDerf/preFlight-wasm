# preFlight -> WebAssembly superbuild (Cubby Slicer engine ABI, see
# cubby-slicer/docs/ENGINE-CONTRACT.md).
#
# Configure through scripts/build-wasm.sh, which sources wasm-deps/env.sh and
# runs `emcmake cmake -S wasm -B build-<variant>`.
#
# Memory-safety invariants (every object, deps included):
#   -fwasm-exceptions -sSUPPORT_LONGJMP=wasm, single-threaded, no
#   EMULATE_FUNCTION_POINTER_CASTS, no operator new/delete overrides, and no
#   stub header shadowing a library that is also linked. The only "shim" is the
#   sequential TBB replacement (no TBB library is linked).
cmake_minimum_required(VERSION 3.22)
project(preflight_wasm C CXX)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "Configure with emcmake (see scripts/build-wasm.sh)")
endif ()

set(BUILD_VARIANT "release" CACHE STRING "release | debug")
set_property(CACHE BUILD_VARIANT PROPERTY STRINGS release debug)
if (NOT BUILD_VARIANT MATCHES "^(release|debug)$")
    message(FATAL_ERROR "BUILD_VARIANT must be release or debug")
endif ()

get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PREFLIGHT_SRC "${_root}/../preflight" CACHE PATH "preFlight source checkout (v1.3.0 + patches/preflight-wasm.patch)")
set(WASM_BRIDGE_DIR "${_root}/bridge/common" CACHE PATH "Engine-neutral bridge core (cs_common.hpp)")
if (DEFINED ENV{WASM_DEPS_PREFIX})
    set(_deps_default "$ENV{WASM_DEPS_PREFIX}")
else ()
    if (EXISTS "${_root}/../wasm-deps/install")
        set(_deps_default "${_root}/../wasm-deps/install")
    else ()
        set(_deps_default "${_root}/toolchain/install")
    endif ()
endif ()
set(WASM_DEPS_PREFIX "${_deps_default}" CACHE PATH "Shared wasm dependency prefix (wasm-deps/install)")
get_filename_component(_deps_root "${WASM_DEPS_PREFIX}/.." ABSOLUTE)
set(WASM_CGAL_ROOT "${_deps_root}/cgal-6.1" CACHE PATH "CGAL 6.1 header install")

foreach (_p PREFLIGHT_SRC WASM_BRIDGE_DIR WASM_DEPS_PREFIX WASM_CGAL_ROOT)
    if (NOT EXISTS "${${_p}}")
        message(FATAL_ERROR "${_p} not found: ${${_p}}")
    endif ()
endforeach ()

# ---------------------------------------------------------------- flags
# Exception/longjmp model + Emscripten ports, for every C/C++ object. The port
# flags make <zlib.h>/<png.h>/<jpeglib.h> resolve to the real port headers.
set(WASM_COMMON_FLAGS -fwasm-exceptions -sSUPPORT_LONGJMP=wasm -sUSE_ZLIB=1 -sUSE_LIBPNG=1 -sUSE_LIBJPEG=1)
add_compile_options(${WASM_COMMON_FLAGS} -Wno-unknown-warning-option -Wno-deprecated-declarations
                    -Wno-unused-command-line-argument)
add_link_options(${WASM_COMMON_FLAGS})

if (BUILD_VARIANT STREQUAL "debug")
    # NDEBUG stays defined so the debug build exercises the same engine code
    # paths as release; the memory checking comes from SAFE_HEAP/ASSERTIONS.
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_RELEASE "-O1 -g2 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O1 -g2 -DNDEBUG" CACHE STRING "" FORCE)
else ()
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)
endif ()

# Boost.Log was built with these (wasm-deps/build-deps.sh); consumers must see
# the same macros so they bind to the same inline namespace (v2s_st).
add_compile_definitions(
    BOOST_LOG_NO_THREADS
    BOOST_LOG_WITHOUT_SYSLOG BOOST_LOG_WITHOUT_EVENT_LOG
    BOOST_LOG_WITHOUT_DEBUG_OUTPUT BOOST_LOG_WITHOUT_IPC
    BOOST_LOG_STATIC_LINK
)

# ------------------------------------------------------- dependency lookup
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake") # TBB shim + Emscripten ports
list(PREPEND CMAKE_PREFIX_PATH "${WASM_DEPS_PREFIX}" "${WASM_CGAL_ROOT}")
# The Emscripten toolchain restricts find_* to CMAKE_FIND_ROOT_PATH (the
# sysroot); add our prefixes as roots so nothing from the host leaks in.
list(APPEND CMAKE_FIND_ROOT_PATH "${WASM_DEPS_PREFIX}" "${WASM_CGAL_ROOT}")

set(Boost_ROOT "${WASM_DEPS_PREFIX}" CACHE PATH "" FORCE)
set(Boost_NO_SYSTEM_PATHS ON CACHE BOOL "" FORCE)
set(Boost_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(Boost_USE_MULTITHREADED OFF CACHE BOOL "" FORCE) # built threading=single
set(Boost_USE_STATIC_RUNTIME ON CACHE BOOL "" FORCE)  # built runtime-link=static (no-op for wasm)
set(CGAL_DIR "${WASM_CGAL_ROOT}/lib/cmake/CGAL" CACHE PATH "" FORCE)

# ------------------------------------------------------- preFlight options
set(SLIC3R_GUI OFF CACHE BOOL "" FORCE)
set(SLIC3R_STATIC ON CACHE BOOL "" FORCE)            # BOOST_LOG_STATIC_LINK, static Qhull
set(SLIC3R_PCH OFF CACHE BOOL "" FORCE)
set(SLIC3R_ENABLE_FORMAT_STEP OFF CACHE BOOL "" FORCE) # no OCCT
set(SLIC3R_PYTHON_PREPROCESSOR OFF CACHE BOOL "" FORCE) # no CPython/pybind11
set(SLIC3R_DESKTOP_INTEGRATION OFF CACHE BOOL "" FORCE)
set(SLIC3R_ENC_CHECK OFF CACHE BOOL "" FORCE)      # host-side source check tool; would be cross-compiled
set(SLIC3R_ASAN OFF CACHE BOOL "" FORCE)
set(SLIC3R_UBSAN OFF CACHE BOOL "" FORCE)

# The TBB shim is the only TBB anywhere in the build (natively TBB headers are
# on the system include path, and e.g. libslic3r_cgal includes them without
# linking TBB::tbb), so make it globally visible.
include_directories(SYSTEM "${CMAKE_CURRENT_LIST_DIR}/tbb_shim")

add_subdirectory("${PREFLIGHT_SRC}" preflight EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------- engine
add_subdirectory(../bridge bridge)
