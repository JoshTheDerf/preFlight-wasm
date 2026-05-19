# Minimal PNG shim for Emscripten/WASM.
# Satisfies find_package(PNG REQUIRED) without linking real libs.

set(PNG_FOUND TRUE)
set(PNG_INCLUDE_DIR "")
set(PNG_LIBRARY "")
# Back-compat vars some projects expect:
set(PNG_PNG_INCLUDE_DIR "")
set(PNG_LIBRARIES "")
# Provide an imported target expected by consumers
if(NOT TARGET PNG::PNG)
  add_library(PNG::PNG INTERFACE IMPORTED)
  set_target_properties(PNG::PNG PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
