#ifndef __SLIC3R_VERSION_H
#define __SLIC3R_VERSION_H

#define SLIC3R_APP_NAME "OrcaSlicer"
#define SLIC3R_APP_KEY "OrcaSlicer"
#define SLIC3R_VERSION "01.10.01.50"
#define SoftFever_VERSION "01.10.01.50"
#ifndef GIT_COMMIT_HASH
    #define GIT_COMMIT_HASH "0000000" // 0000000 means uninitialized
#endif
#define SLIC3R_BUILD_ID "WASM"
#define BBL_RELEASE_TO_PUBLIC 1
#define BBL_INTERNAL_TESTING 0
#define ORCA_CHECK_GCODE_PLACEHOLDERS 1

#endif /* __SLIC3R_VERSION_H */