# cereal is header-only; its installed cerealConfigVersion.cmake rejects a
# 32-bit (wasm32) consumer because it was generated on a 64-bit host. Use the
# headers from the shared prefix directly.
find_path(CEREAL_INCLUDE_DIR cereal/cereal.hpp PATHS "${WASM_DEPS_PREFIX}/include" NO_DEFAULT_PATH)
if (NOT CEREAL_INCLUDE_DIR)
  message(FATAL_ERROR "cereal headers not found in ${WASM_DEPS_PREFIX}/include")
endif ()
if (NOT TARGET cereal::cereal)
  add_library(cereal::cereal INTERFACE IMPORTED GLOBAL)
  set_target_properties(cereal::cereal PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${CEREAL_INCLUDE_DIR}")
endif ()
set(cereal_FOUND TRUE)
