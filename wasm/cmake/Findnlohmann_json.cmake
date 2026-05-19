# Header-only nlohmann_json shim for the WASM build.
# Resolves to the bundled single-header in ../deps/nlohmann_json.

set(_nj_root "${CMAKE_CURRENT_LIST_DIR}/../../deps/nlohmann_json")

if (NOT EXISTS "${_nj_root}/include/nlohmann/json.hpp")
    message(FATAL_ERROR "nlohmann_json single header not found at ${_nj_root}/include/nlohmann/json.hpp")
endif ()

if (NOT TARGET nlohmann_json)
    add_library(nlohmann_json INTERFACE)
    target_include_directories(nlohmann_json INTERFACE "${_nj_root}/include")
endif ()
if (NOT TARGET nlohmann_json::nlohmann_json)
    add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
endif ()

set(nlohmann_json_FOUND TRUE)
set(nlohmann_json_VERSION 3.11.3)
