#ifndef ORCA_WASM_WRAP_H
#define ORCA_WASM_WRAP_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void* OS_Mesh;
typedef void* OS_Result;

// Load mesh from memory buffer (STL format)
// Returns OS_Mesh handle on success, nullptr on failure
// If err is provided, error message will be written to it
OS_Mesh   os_load_mesh(const uint8_t* bytes, size_t len, char* err, size_t err_cap);

// Slice the mesh with basic settings
// settings_json can be nullptr for defaults
// Returns OS_Result handle on success, nullptr on failure
OS_Result os_slice_basic(OS_Mesh mesh, const char* settings_json, char* err, size_t err_cap);

// Get G-code from slicing result
// If out is nullptr, returns the size needed
// Otherwise copies G-code to out buffer and returns bytes copied
size_t    os_result_gcode(OS_Result r, uint8_t* out, size_t cap);

// Get layer preview data (TODO: implement)
size_t    os_result_preview_layer(OS_Result r, int layer_index, uint8_t* out, size_t cap);

// Free mesh memory
void      os_free_mesh(OS_Mesh m);

// Free result memory  
void      os_free_result(OS_Result r);

#ifdef __cplusplus
}
#endif
#endif
