## Minimal FindEigen3.cmake for WASM build
# Prefers in-tree Eigen sources but honours a pre-set EIGEN3_INCLUDE_DIR.

set(_eigen_search_roots)
if(EIGEN3_INCLUDE_DIR)
  list(APPEND _eigen_search_roots "${EIGEN3_INCLUDE_DIR}")
endif()

list(APPEND _eigen_search_roots
  "${PROJECT_SOURCE_DIR}/deps_src/eigen3"
  "${PROJECT_SOURCE_DIR}/deps_src/eigen"
  "${PROJECT_SOURCE_DIR}/../orca/deps_src/eigen3"
  "${PROJECT_SOURCE_DIR}/../orca/deps_src/eigen"
  "${CMAKE_SOURCE_DIR}/../deps/toolchain-wasm/install/include/eigen3"
)

set(_eigen_resolved "")
foreach(_root IN LISTS _eigen_search_roots)
  if(EXISTS "${_root}/Eigen/Core")
    set(_eigen_resolved "${_root}")
    break()
  endif()
endforeach()

if(_eigen_resolved)
  set(EIGEN3_INCLUDE_DIR "${_eigen_resolved}" CACHE PATH "Eigen include dir" FORCE)
  set(EIGEN3_INCLUDE_DIRS "${_eigen_resolved}" CACHE PATH "Eigen include dirs" FORCE)
  set(EIGEN3_FOUND TRUE)
  set(EIGEN3_VERSION_OK TRUE)
else()
  set(EIGEN3_FOUND FALSE)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Eigen3 DEFAULT_MSG EIGEN3_INCLUDE_DIR)

if(EIGEN3_FOUND)
  # Provide a namespace-friendly imported target for downstream consumers
  if(NOT TARGET Eigen3::Eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${EIGEN3_INCLUDE_DIR}")
  endif()
endif()
