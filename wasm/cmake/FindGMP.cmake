# Minimal GMP finder for the WASM toolchain.
# Uses deps/toolchain-wasm/install by default or the path provided via
# WASM_MATH_PREFIX (CMake variable or env var).

set(_gmp_prefix "${WASM_MATH_PREFIX}")
if(NOT _gmp_prefix AND DEFINED ENV{WASM_MATH_PREFIX})
  set(_gmp_prefix "$ENV{WASM_MATH_PREFIX}")
endif()
if(NOT _gmp_prefix)
  get_filename_component(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
  set(_gmp_prefix "${_repo_root}/deps/toolchain-wasm/install")
endif()

set(GMP_INCLUDE_DIR "${_gmp_prefix}/include")
set(GMP_LIBRARY "${_gmp_prefix}/lib/libgmp.a")

if(EXISTS "${GMP_INCLUDE_DIR}/gmp.h" AND EXISTS "${GMP_LIBRARY}")
  set(GMP_FOUND TRUE)
  set(GMP_INCLUDE_DIRS "${GMP_INCLUDE_DIR}")
  set(GMP_LIBRARIES "${GMP_LIBRARY}")
  if(NOT TARGET GMP::GMP)
    add_library(GMP::GMP INTERFACE IMPORTED)
    set_target_properties(GMP::GMP PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${GMP_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "${GMP_LIBRARY}"
    )
  endif()
else()
  set(GMP_FOUND FALSE)
endif()
