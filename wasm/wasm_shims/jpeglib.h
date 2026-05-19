#ifndef WASM_SHIMS_JPEGLIB_H
#define WASM_SHIMS_JPEGLIB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int boolean;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef unsigned int JDIMENSION;

typedef struct jpeg_error_mgr {
    int dummy;
} jpeg_error_mgr;

typedef struct jpeg_compress_struct {
    jpeg_error_mgr *err;
    unsigned int    image_width;
    unsigned int    image_height;
    int             input_components;
    int             in_color_space;
    unsigned char **dest_buffer;
    unsigned long  *dest_size;
    size_t          next_output_byte;
} jpeg_compress_struct;

typedef jpeg_compress_struct *j_compress_ptr;

typedef jpeg_error_mgr *j_error_ptr;

typedef unsigned char *JSAMPROW;

typedef JSAMPROW *JSAMPARRAY;

static inline jpeg_error_mgr *jpeg_std_error(jpeg_error_mgr *err)
{
    return err;
}

static inline void jpeg_create_compress(j_compress_ptr cinfo)
{
    (void)cinfo;
}

static inline void jpeg_mem_dest(j_compress_ptr cinfo, unsigned char **outbuffer, unsigned long *outsize)
{
    if (cinfo == NULL)
        return;
    cinfo->dest_buffer = outbuffer;
    cinfo->dest_size = outsize;
    cinfo->next_output_byte = 0;
}

static inline void jpeg_set_defaults(j_compress_ptr cinfo)
{
    (void)cinfo;
}

static inline void jpeg_set_quality(j_compress_ptr cinfo, int quality, boolean force_baseline)
{
    (void)cinfo;
    (void)quality;
    (void)force_baseline;
}

static inline void jpeg_start_compress(j_compress_ptr cinfo, boolean write_all_tables)
{
    (void)cinfo;
    (void)write_all_tables;
}

static inline JDIMENSION jpeg_write_scanlines(j_compress_ptr cinfo, JSAMPARRAY scanlines, JDIMENSION num_lines)
{
    if (cinfo == NULL || scanlines == NULL || cinfo->dest_buffer == NULL || *cinfo->dest_buffer == NULL)
        return 0;

    unsigned char *buffer = *cinfo->dest_buffer;
    size_t max_size = (cinfo->dest_size != NULL) ? (size_t)(*cinfo->dest_size) : (size_t)0;
    size_t offset = cinfo->next_output_byte;
    size_t row_bytes = (size_t)cinfo->image_width * (size_t)((cinfo->input_components > 0) ? cinfo->input_components : 1);

    for (JDIMENSION i = 0; i < num_lines; ++i) {
        if (cinfo->dest_size != NULL && (offset + row_bytes > max_size))
            break;
        if (scanlines[i] != NULL && row_bytes > 0) {
            memcpy(buffer + offset, scanlines[i], row_bytes);
            offset += row_bytes;
        }
    }

    cinfo->next_output_byte = offset;
    if (cinfo->dest_size != NULL)
        *cinfo->dest_size = (unsigned long)offset;

    return num_lines;
}

static inline void jpeg_finish_compress(j_compress_ptr cinfo)
{
    if (cinfo == NULL)
        return;
    if (cinfo->dest_size != NULL)
        *cinfo->dest_size = (unsigned long)cinfo->next_output_byte;
}

static inline void jpeg_destroy_compress(j_compress_ptr cinfo)
{
    (void)cinfo;
}

enum {
    JCS_EXT_RGBA = 0
};

#ifdef __cplusplus
}
#endif

#endif
