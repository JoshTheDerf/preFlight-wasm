add_library(preflight_wasm_bridge STATIC preflight_wrap.cpp)

# nlohmann_json header is a single-header bundled at ../deps/nlohmann_json.
target_include_directories(preflight_wasm_bridge PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../deps/nlohmann_json/include
)
if (TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(preflight_wasm_bridge PUBLIC nlohmann_json::nlohmann_json)
endif ()

# Expose bridge headers alongside preFlight core and shim headers so consumers
# (including the standalone slicer executable) compile without chasing include
# paths manually.
target_include_directories(preflight_wasm_bridge BEFORE PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../wasm/wasm_shims
    ${CMAKE_CURRENT_LIST_DIR}/../wasm/wasm_shims/boost_runtime
    ${CMAKE_CURRENT_LIST_DIR}/../wasm/wasm_shims/libslic3r
)

target_include_directories(preflight_wasm_bridge
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/src
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/src/clipper2/include
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/admesh
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/libbgcode
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/libbgcode/LibBGCode
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/libigl
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/libnest2d/include
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/semver
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/tcbspan
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/fast_float
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/int128
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/agg
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/ankerl
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/miniz
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/localesutils
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps/stb_image
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/deps_src
        ${CMAKE_CURRENT_LIST_DIR}/../../preflight/deps_src/eigen
        ${CMAKE_CURRENT_LIST_DIR}/../deps/eigen
        ${CMAKE_CURRENT_LIST_DIR}/../deps/nanosvg
        ${BOOST_INC}
)
