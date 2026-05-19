# Minimal OpenCASCADE shim for Emscripten/WASM
set(OpenCASCADE_FOUND TRUE)
set(OpenCASCADE_INCLUDE_DIR "")
set(OpenCASCADE_INCLUDE_DIRS "")
set(OpenCASCADE_LIBRARIES "")
# Pretend major components exist
foreach(t IN ITEMS TKernel TKMath TKGeomBase TKGeomAlgo TKMesh TKBO)
  set(OpenCASCADE_${t}_FOUND TRUE)
endforeach()
# Create dummy targets
foreach(t IN ITEMS OpenCASCADE TKernel TKMath TKGeomBase TKGeomAlgo TKMesh TKBO)
  if(NOT TARGET OpenCASCADE::${t})
    add_library(OpenCASCADE::${t} INTERFACE IMPORTED)
  endif()
endforeach()
