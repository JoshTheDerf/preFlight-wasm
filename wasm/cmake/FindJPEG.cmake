# Minimal JPEG shim for Emscripten/WASM
set(JPEG_FOUND TRUE)
set(JPEG_INCLUDE_DIR "")
set(JPEG_LIBRARY "")
set(JPEG_LIBRARIES "")
if(NOT TARGET JPEG::JPEG)
  add_library(JPEG::JPEG INTERFACE IMPORTED)
endif()
