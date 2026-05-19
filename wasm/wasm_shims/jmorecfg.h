#ifndef WASM_SHIMS_JMORECFG_H
#define WASM_SHIMS_JMORECFG_H

/* libjpeg jmorecfg.h shim — empty in WASM build (no JPEG codec available).
   The bridge does not call any JPEG codec entry points; this header exists
   solely so the `#include <jmorecfg.h>` in Thumbnails.cpp resolves. */

#include "jpeglib.h"

#endif
