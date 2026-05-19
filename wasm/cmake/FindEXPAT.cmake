# Minimal EXPAT shim for Emscripten/WASM
set(EXPAT_FOUND TRUE)
set(EXPAT_INCLUDE_DIR "")
set(EXPAT_LIBRARY "")
set(EXPAT_INCLUDE_DIRS "")
set(EXPAT_LIBRARIES "")
# Modern imported target
if(NOT TARGET EXPAT::EXPAT)
  add_library(EXPAT::EXPAT INTERFACE IMPORTED)
endif()
