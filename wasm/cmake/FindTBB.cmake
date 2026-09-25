# TBB for the single-threaded WebAssembly build: header-only sequential shim
# (wasm/tbb_shim). No TBB library exists or is linked, so this does not shadow
# anything that is also linked.
set(_tbb_shim "${CMAKE_CURRENT_LIST_DIR}/../tbb_shim")
foreach(_t tbb tbbmalloc tbbmalloc_proxy)
  if(NOT TARGET TBB::${_t})
    add_library(TBB::${_t} INTERFACE IMPORTED GLOBAL)
    set_target_properties(TBB::${_t} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_tbb_shim}")
  endif()
endforeach()
set(TBB_FOUND TRUE)
set(TBB_VERSION 2021.13.0)
set(TBB_INCLUDE_DIRS "${_tbb_shim}")
set(TBB_LIBRARIES TBB::tbb)
