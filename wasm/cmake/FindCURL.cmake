# Minimal CURL shim for Emscripten/WASM.
# Satisfies find_package(CURL REQUIRED) without linking real libs.

set(CURL_FOUND TRUE)
set(CURL_VERSION_STRING "7.0")
set(CURL_INCLUDE_DIR "")
set(CURL_LIBRARY "")

# Some projects expect CURL::libcurl imported target.
if(NOT TARGET CURL::libcurl)
  add_library(CURL::libcurl INTERFACE IMPORTED)
  set_target_properties(CURL::libcurl PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ""
  )
endif()
