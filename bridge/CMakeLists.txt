# The engine module: cs_bridge.cpp + libslic3r -> slicer.mjs / slicer.wasm.
find_package(nlohmann_json 3.11 REQUIRED CONFIG)

add_executable(slicer cs_bridge.cpp "${WASM_BRIDGE_DIR}/cs_fast_strtod.cpp")
set_target_properties(slicer PROPERTIES OUTPUT_NAME "slicer" SUFFIX ".mjs"
                                        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_features(slicer PRIVATE cxx_std_20)
target_include_directories(slicer PRIVATE "${WASM_BRIDGE_DIR}" "${CMAKE_CURRENT_LIST_DIR}/../../preflight/bundled_deps")
target_compile_definitions(slicer PRIVATE CS_BUILD_VARIANT="${BUILD_VARIANT}")
# libslic3r's PRIVATE deps (Boost, Qhull, CGAL glue, expat, heatshrink, ports)
# propagate to this link as LINK_ONLY because it is a static library.
target_link_libraries(slicer PRIVATE libslic3r libslic3r_cgal nlohmann_json::nlohmann_json)

set(_exports "_cs_version,_cs_describe_config,_cs_slice,_cs_eval_condition,_cs_free,_malloc,_free")
set(_runtime "HEAPU8,HEAP32,HEAPU32,UTF8ToString,stringToUTF8,lengthBytesUTF8,FS")
# Emscripten >= 4 ignores Module keys that are not listed here (release builds, silently).
# (csProgress is not an Emscripten setting: the EM_JS hook reads Module["csProgress"] at call time.)
set(_incoming "locateFile,print,printErr,instantiateWasm,wasmBinary,getPreloadedPackage,onAbort,monitorRunDependencies,setStatus,noInitialRun,preRun,postRun")

target_link_options(slicer PRIVATE
    # musl strtod parses via 128-bit soft-float long double; route it to fast_float.
    -Wl,--wrap=strtod -Wl,--wrap=strtof
    -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=4GB
    -sINITIAL_MEMORY=128MB  # 64MB cannot hold the 64MB stack + static data
    -sSTACK_SIZE=64MB
    -sABORTING_MALLOC=0
    -sMALLOC=dlmalloc
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=PreflightModule
    -sENVIRONMENT=web,worker,node
    -sEXIT_RUNTIME=0
    -sINVOKE_RUN=0
    "-sEXPORTED_FUNCTIONS=[${_exports}]"
    "-sEXPORTED_RUNTIME_METHODS=[${_runtime}]"
    "-sINCOMING_MODULE_JS_API=[${_incoming}]"
)
if (BUILD_VARIANT STREQUAL "debug")
    target_link_options(slicer PRIVATE -O1 -g2 -sASSERTIONS=2 -sSAFE_HEAP=1 -sSTACK_OVERFLOW_CHECK=2)
else ()
    set(WASM_LINK_OPT "-O3" CACHE STRING "Link-time (wasm-opt) optimization level")
    # STACK_OVERFLOW_CHECK=2 (not 1): the stack cannot be placed first, so without the per-function
    # check an overflow would silently overwrite static data instead of trapping.
    target_link_options(slicer PRIVATE ${WASM_LINK_OPT} -sSTACK_OVERFLOW_CHECK=2)
endif ()
