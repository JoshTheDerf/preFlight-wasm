# Minimal FindBoost override for Emscripten/WASM.
# It points to your local Boost install and marks the required components FOUND.

if(NOT DEFINED BOOST_PREFIX)
  set(BOOST_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../deps/boost-wasm/install")
endif()
set(BOOST_INC "${BOOST_PREFIX}/include")
set(BOOST_LIB "${BOOST_PREFIX}/lib")

# Try staged Boost headers first; if missing, fall back to wasm shims
if(NOT EXISTS "${BOOST_INC}/boost/version.hpp")
  # Fallback to lightweight shim headers sufficient for WASM build
  set(_BOOST_SHIMS_CORE "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims/boost")
  set(_BOOST_SHIMS_RUNTIME "${CMAKE_CURRENT_LIST_DIR}/../wasm_shims/boost_runtime")
  if(EXISTS "${_BOOST_SHIMS_CORE}" OR EXISTS "${_BOOST_SHIMS_RUNTIME}")
    # Merge shim include roots
    set(BOOST_INC "${_BOOST_SHIMS_CORE};${_BOOST_SHIMS_RUNTIME}")
  else()
    message(FATAL_ERROR "Boost headers not found. Expected at ${BOOST_INC} or shims at ${_BOOST_SHIMS_CORE} / ${_BOOST_SHIMS_RUNTIME}.")
  endif()
endif()

set(Boost_FOUND TRUE)
set(Boost_VERSION 108300)
set(Boost_INCLUDE_DIR  "${BOOST_INC}")
set(Boost_INCLUDE_DIRS "${BOOST_INC}")
set(Boost_LIBRARIES "")
function(_boost_add COMPONENT UPPER LIBNAME)
  set(_path "${BOOST_LIB}/${LIBNAME}")
  if(EXISTS "${_path}")
    set(Boost_${UPPER}_FOUND TRUE PARENT_SCOPE)
    set(Boost_${UPPER}_LIBRARY "${_path}" PARENT_SCOPE)
    set(_acc "${Boost_LIBRARIES}")
    list(APPEND _acc "${_path}")
    set(Boost_LIBRARIES "${_acc}" PARENT_SCOPE)
  else()
    set(Boost_${UPPER}_FOUND FALSE PARENT_SCOPE)
  endif()
endfunction()

_boost_add(system          SYSTEM          libboost_system.a)
_boost_add(filesystem      FILESYSTEM      libboost_filesystem.a)
_boost_add(thread          THREAD          libboost_thread.a)
_boost_add(regex           REGEX           libboost_regex.a)
_boost_add(chrono          CHRONO          libboost_chrono.a)
_boost_add(atomic          ATOMIC          libboost_atomic.a)
_boost_add(date_time       DATE_TIME       libboost_date_time.a)
_boost_add(iostreams       IOSTREAMS       libboost_iostreams.a)
_boost_add(program_options PROGRAM_OPTIONS libboost_program_options.a)
_boost_add(log             LOG             libboost_log.a)
_boost_add(log_setup       LOG_SETUP       libboost_log_setup.a)
# Header-only: nowide
set(Boost_NOWIDE_FOUND TRUE)

# locale optional
if(EXISTS "${BOOST_LIB}/libboost_locale.a")
  _boost_add(locale LOCALE libboost_locale.a)
else()
  set(Boost_LOCALE_FOUND TRUE) # treat as header-only shim in WASM
endif()

# Mark required components as found when using shims, to satisfy REQUIRED
if(NOT EXISTS "${BOOST_PREFIX}/include/boost/version.hpp")
  foreach(_comp IN ITEMS SYSTEM FILESYSTEM THREAD REGEX CHRONO ATOMIC DATE_TIME IOSTREAMS PROGRAM_OPTIONS LOG LOG_SETUP NOWIDE)
    set(Boost_${_comp}_FOUND TRUE)
  endforeach()
endif()
# Header-only umbrella target for Boost
if(NOT TARGET Boost::boost)
  add_library(Boost::boost INTERFACE IMPORTED)
  set_target_properties(Boost::boost PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${BOOST_INC}"
  )
endif()
