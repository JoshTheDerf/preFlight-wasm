# Minimal Zlib shim for Emscripten/WASM.
# Satisfies find_package(ZLIB REQUIRED) without linking real libs.

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR "")
set(ZLIB_LIBRARY "")
# Back-compat vars some Find modules export:
set(ZLIB_INCLUDE_DIRS "")
set(ZLIB_LIBRARIES "")
# Provide the imported target expected by consumers
if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED)
  set_target_properties(ZLIB::ZLIB PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
