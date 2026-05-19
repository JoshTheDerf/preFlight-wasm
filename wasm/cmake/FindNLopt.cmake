# Minimal NLopt shim for Emscripten/WASM.
# Satisfies find_package(NLopt REQUIRED) without linking real libs.

set(NLopt_FOUND TRUE)
set(NLOPT_FOUND TRUE)         # some modules use this spelling
set(NLopt_INCLUDE_DIR "")
set(NLopt_LIBRARY "")
set(NLopt_LIBRARIES "")
# Provide imported target expected by consumers
if(NOT TARGET NLopt::nlopt)
  add_library(NLopt::nlopt INTERFACE IMPORTED)
  set_target_properties(NLopt::nlopt PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
