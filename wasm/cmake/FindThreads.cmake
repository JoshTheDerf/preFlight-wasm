# Minimal Threads shim for Emscripten/WASM
set(Threads_FOUND TRUE)
if(NOT TARGET Threads::Threads)
  add_library(Threads::Threads INTERFACE IMPORTED)
  # Propagate -pthread to dependents (Emscripten understands this)
  target_compile_options(Threads::Threads INTERFACE -pthread)
  target_link_options(Threads::Threads INTERFACE -pthread)
endif()
