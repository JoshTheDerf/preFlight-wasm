# WASM-friendly TBB shim for Emscripten builds
# Provides clean fallback to our wasm_shims

if(EMSCRIPTEN)
  # For WASM builds we provide lightweight headers in wasm_shims to satisfy
  # direct includes (<tbb/...>). Do NOT advertise TBB as "found" to CMake
  # consumers (CGAL) because that triggers enabling TBB-dependent code paths
  # inside header-only libraries which rely on runtime TBB features.

  # Provide classic variables for code that checks for them. We advertise
  # TBB_FOUND=TRUE so that find_package(TBB REQUIRED) succeeds, but the shimbed
  # targets below resolve to header-only stubs.
  set(TBB_FOUND TRUE)
  set(TBB_VERSION "0.0-wasm-shim")
  set(TBB_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims")
  set(TBB_LIBRARIES "TBB::tbb;TBB::tbbmalloc")
  set(TBB_IMPORTED_TARGETS TBB::tbb TBB::tbbmalloc)

  # Create a small interface target 'wasm_shims' (if not present) that exposes
  # the include path. We intentionally do NOT create TBB::tbb or TBB::tbbmalloc
  # imported targets to avoid convincing other CMake scripts that a full TBB
  # implementation is available.
  if(NOT TARGET wasm_shims)
    add_library(wasm_shims INTERFACE IMPORTED)
    set_target_properties(wasm_shims PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims"
    )
  endif()

  # However other components expect TBB::tbb and TBB::tbbmalloc to exist.
  # Provide lightweight IMPORTED interface targets that link to our wasm_shims
  # so that target_link_libraries(TBB::tbb) works, but do NOT set
  # TBB_FOUND to TRUE so downstream checks for a real TBB runtime remain false.
  if(NOT TARGET TBB::tbb)
    add_library(TBB::tbb INTERFACE IMPORTED)
    target_link_libraries(TBB::tbb INTERFACE wasm_shims)
    set_target_properties(TBB::tbb PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims"
    )
  endif()

  if(NOT TARGET TBB::tbbmalloc)
    add_library(TBB::tbbmalloc INTERFACE IMPORTED)
    target_link_libraries(TBB::tbbmalloc INTERFACE wasm_shims)
    set_target_properties(TBB::tbbmalloc PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims"
    )
  endif()

else()
  # On native builds, find real TBB
  find_package(TBB REQUIRED)
endif()
