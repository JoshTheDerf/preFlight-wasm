# ZLIB from the Emscripten port (-sUSE_ZLIB=1). The port headers live in the Emscripten
# sysroot; the option must be present at compile AND link time.
if(NOT TARGET ZLIB::ZLIB)
  add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
  set_target_properties(ZLIB::ZLIB PROPERTIES
    INTERFACE_COMPILE_OPTIONS "-sUSE_ZLIB=1"
    INTERFACE_LINK_OPTIONS "-sUSE_ZLIB=1")
endif()
set(ZLIB_FOUND TRUE)
set(ZLIB_LIBRARIES ZLIB::ZLIB)
set(ZLIB_LIBRARY ZLIB::ZLIB)
set(ZLIB_INCLUDE_DIRS "")
set(ZLIB_INCLUDE_DIR "")
