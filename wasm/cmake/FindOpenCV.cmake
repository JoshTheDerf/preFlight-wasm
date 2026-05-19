# Minimal OpenCV shim for Emscripten/WASM (headless).
# Satisfies find_package(OpenCV REQUIRED [COMPONENTS ...]) without real libs.

set(OpenCV_FOUND TRUE)
set(OpenCV_VERSION "4.0.0")
set(OpenCV_INCLUDE_DIRS "")
set(OpenCV_LIBS "")
# Common component flags some CMakeLists probe:
set(OpenCV_CORE_FOUND TRUE)
set(OpenCV_IMGPROC_FOUND TRUE)
set(OpenCV_IMGCODECS_FOUND TRUE)
set(OpenCV_HIGHGUI_FOUND TRUE)
set(OpenCV_VIDEOIO_FOUND TRUE)
# Provide imported targets most projects reference
foreach(tgt IN ITEMS opencv opencv_core opencv_imgproc opencv_imgcodecs opencv_highgui opencv_videoio)
  if(NOT TARGET OpenCV::${tgt})
    add_library(OpenCV::${tgt} INTERFACE IMPORTED)
    set_target_properties(OpenCV::${tgt} PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ""
    )
  endif()
endforeach()
