# Minimal GLEW shim for Emscripten/WASM (no desktop OpenGL).
# Satisfies find_package(GLEW REQUIRED) without linking real libs.
set(GLEW_FOUND TRUE)
set(GLEW_INCLUDE_DIR "")
set(GLEW_LIBRARIES "")
# Provide imported target some consumers expect
if(NOT TARGET GLEW::GLEW)
  add_library(GLEW::GLEW INTERFACE IMPORTED)
  set_target_properties(GLEW::GLEW PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
