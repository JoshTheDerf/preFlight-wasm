# JPEG from the Emscripten port (-sUSE_LIBJPEG=1). The port headers live in the Emscripten
# sysroot; the option must be present at compile AND link time.
if(NOT TARGET JPEG::JPEG)
  add_library(JPEG::JPEG INTERFACE IMPORTED GLOBAL)
  set_target_properties(JPEG::JPEG PROPERTIES
    INTERFACE_COMPILE_OPTIONS "-sUSE_LIBJPEG=1"
    INTERFACE_LINK_OPTIONS "-sUSE_LIBJPEG=1")
endif()
set(JPEG_FOUND TRUE)
set(JPEG_LIBRARIES JPEG::JPEG)
set(JPEG_LIBRARY JPEG::JPEG)
set(JPEG_INCLUDE_DIRS "")
set(JPEG_INCLUDE_DIR "")
