# Minimal MPFR finder for the WASM toolchain.
# Uses deps/toolchain-wasm/install by default or the path provided via
# WASM_MATH_PREFIX (CMake variable or env var).

set(_mpfr_prefix "${WASM_MATH_PREFIX}")
if(NOT _mpfr_prefix AND DEFINED ENV{WASM_MATH_PREFIX})
  set(_mpfr_prefix "$ENV{WASM_MATH_PREFIX}")
endif()
if(NOT _mpfr_prefix)
  get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  set(_mpfr_prefix "${_repo_root}/deps/toolchain-wasm/install")
endif()

set(MPFR_INCLUDE_DIR "${_mpfr_prefix}/include")
set(MPFR_LIBRARY "${_mpfr_prefix}/lib/libmpfr.a")

if(EXISTS "${MPFR_INCLUDE_DIR}/mpfr.h" AND EXISTS "${MPFR_LIBRARY}")
  set(MPFR_FOUND TRUE)
  set(MPFR_INCLUDE_DIRS "${MPFR_INCLUDE_DIR}")
  set(MPFR_LIBRARIES "${MPFR_LIBRARY}")
  if(NOT TARGET MPFR::MPFR)
    add_library(MPFR::MPFR INTERFACE IMPORTED)
    set_target_properties(MPFR::MPFR PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${MPFR_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "${MPFR_LIBRARY}"
    )
  endif()
else()
  set(MPFR_FOUND FALSE)
endif()
