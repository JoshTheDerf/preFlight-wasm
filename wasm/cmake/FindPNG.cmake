# PNG from the Emscripten port (-sUSE_LIBPNG=1). The port headers live in the Emscripten
# sysroot; the option must be present at compile AND link time.
if(NOT TARGET PNG::PNG)
  add_library(PNG::PNG INTERFACE IMPORTED GLOBAL)
  set_target_properties(PNG::PNG PROPERTIES
    INTERFACE_COMPILE_OPTIONS "-sUSE_LIBPNG=1"
    INTERFACE_LINK_OPTIONS "-sUSE_LIBPNG=1")
endif()
set(PNG_FOUND TRUE)
set(PNG_PNG_INCLUDE_DIR "")
set(PNG_LIBRARIES PNG::PNG)
set(PNG_LIBRARY PNG::PNG)
set(PNG_INCLUDE_DIRS "")
set(PNG_INCLUDE_DIR "")
