# Minimal libnoise shim for Emscripten/WASM
set(libnoise_FOUND TRUE)
set(LIBNOISE_FOUND TRUE)
set(LIBNOISE_INCLUDE_DIR "")
set(LIBNOISE_LIBRARY "")
set(LIBNOISE_LIBRARIES "")
if(NOT TARGET libnoise::libnoise)
  add_library(libnoise::libnoise INTERFACE IMPORTED)
endif()
# Provide the alias name Orca expects
if(NOT TARGET noise::noise)
  add_library(noise::noise INTERFACE IMPORTED)
endif()
