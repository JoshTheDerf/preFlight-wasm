# heatshrink WASM shim — resolves to the target produced by the
# add_subdirectory(deps/heatshrink-src) call in the top-level WASM CMakeLists.

if (TARGET heatshrink::heatshrink_dynalloc)
    set(heatshrink_FOUND TRUE)
    return()
endif ()

if (TARGET heatshrink_dynalloc)
    if (NOT TARGET heatshrink::heatshrink_dynalloc)
        add_library(heatshrink::heatshrink_dynalloc ALIAS heatshrink_dynalloc)
    endif ()
    set(heatshrink_FOUND TRUE)
    return()
endif ()

set(heatshrink_FOUND FALSE)
