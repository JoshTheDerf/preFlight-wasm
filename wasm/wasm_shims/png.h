#ifndef PNG_H
#define PNG_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PNG_LIBPNG_VER_STRING "1.6.0"
#define PNG_LIBPNG_VER_MINOR 6

#define PNG_COLOR_TYPE_GRAY 0
#define PNG_COLOR_TYPE_RGB 2
#define PNG_COLOR_TYPE_PALETTE 3
#define PNG_COLOR_TYPE_RGB_ALPHA 6

#define PNG_INTERLACE_NONE 0
#define PNG_COMPRESSION_TYPE_DEFAULT 0
#define PNG_FILTER_TYPE_DEFAULT 0
#define PNG_TRANSFORM_IDENTITY 0

typedef unsigned char png_byte;
typedef png_byte *png_bytep;
typedef const png_byte *png_const_bytep;
typedef size_t png_size_t;
typedef unsigned int png_uint_32;
typedef void *png_voidp;
typedef const void *png_const_voidp;
typedef png_bytep *png_bytepp;

typedef struct png_struct_def png_struct;
typedef png_struct *png_structp;
typedef png_struct **png_structpp;

typedef struct png_info_def png_info;
typedef png_info *png_infop;
typedef png_info **png_infopp;

typedef void (*png_rw_ptr)(png_structp, png_bytep, png_size_t);

typedef struct png_info_def {
    png_uint_32 width;
    png_uint_32 height;
    int bit_depth;
    int color_type;
    int interlace_type;
    int compression_type;
    int filter_type;
    png_size_t rowbytes;
    png_bytepp rows;
} png_info;

typedef struct png_struct_def {
    png_rw_ptr read_fn;
    png_voidp io_ptr;
    png_rw_ptr write_fn;
    png_voidp write_io_ptr;
    jmp_buf jmpbuf;
    int sig_bytes;
    png_info info_store;
} png_struct;

#define png_jmpbuf(png_ptr) ((png_ptr)->jmpbuf)

static inline png_structp png_create_read_struct(const char *ver, png_voidp error_ptr,
                                                 png_voidp error_fn, png_voidp warn_fn)
{
    (void)ver; (void)error_ptr; (void)error_fn; (void)warn_fn;
    png_structp ptr = (png_structp)calloc(1, sizeof(png_struct));
    if (ptr) {
        ptr->info_store.bit_depth = 8;
        ptr->info_store.color_type = PNG_COLOR_TYPE_GRAY;
        ptr->info_store.interlace_type = PNG_INTERLACE_NONE;
        ptr->info_store.compression_type = PNG_COMPRESSION_TYPE_DEFAULT;
        ptr->info_store.filter_type = PNG_FILTER_TYPE_DEFAULT;
    }
    return ptr;
}

static inline png_structp png_create_write_struct(const char *ver, png_voidp error_ptr,
                                                  png_voidp error_fn, png_voidp warn_fn)
{
    return png_create_read_struct(ver, error_ptr, error_fn, warn_fn);
}

static inline png_infop png_create_info_struct(png_structp png_ptr)
{
    (void)png_ptr;
    png_infop info = (png_infop)calloc(1, sizeof(png_info));
    if (info) {
        info->bit_depth = 8;
        info->color_type = PNG_COLOR_TYPE_GRAY;
        info->interlace_type = PNG_INTERLACE_NONE;
        info->compression_type = PNG_COMPRESSION_TYPE_DEFAULT;
        info->filter_type = PNG_FILTER_TYPE_DEFAULT;
    }
    return info;
}

static inline void png_destroy_info_struct(png_structp png_ptr, png_infopp info_ptr_ptr)
{
    (void)png_ptr;
    if (info_ptr_ptr && *info_ptr_ptr) {
        free(*info_ptr_ptr);
        *info_ptr_ptr = NULL;
    }
}

static inline void png_destroy_read_struct(png_structpp png_ptr_ptr, png_infopp info_ptr_ptr,
                                           png_infopp end_info_ptr_ptr)
{
    if (info_ptr_ptr)
        png_destroy_info_struct(png_ptr_ptr ? *png_ptr_ptr : NULL, info_ptr_ptr);
    if (end_info_ptr_ptr)
        png_destroy_info_struct(png_ptr_ptr ? *png_ptr_ptr : NULL, end_info_ptr_ptr);
    if (png_ptr_ptr && *png_ptr_ptr) {
        free(*png_ptr_ptr);
        *png_ptr_ptr = NULL;
    }
}

static inline void png_destroy_write_struct(png_structpp png_ptr_ptr, png_infopp info_ptr_ptr)
{
    png_destroy_read_struct(png_ptr_ptr, info_ptr_ptr, NULL);
}

static inline void png_set_read_fn(png_structp png_ptr, png_voidp io_ptr, png_rw_ptr read_fn)
{
    if (!png_ptr)
        return;
    png_ptr->io_ptr = io_ptr;
    png_ptr->read_fn = read_fn;
}

static inline void png_set_sig_bytes(png_structp png_ptr, int num_bytes)
{
    if (png_ptr)
        png_ptr->sig_bytes = num_bytes;
}

static inline void png_read_info(png_structp png_ptr, png_infop info_ptr)
{
    if (!png_ptr || !info_ptr)
        return;
    info_ptr->width  = png_ptr->info_store.width;
    info_ptr->height = png_ptr->info_store.height;
    info_ptr->bit_depth = png_ptr->info_store.bit_depth;
    info_ptr->color_type = png_ptr->info_store.color_type;
    info_ptr->interlace_type = png_ptr->info_store.interlace_type;
    info_ptr->compression_type = png_ptr->info_store.compression_type;
    info_ptr->filter_type = png_ptr->info_store.filter_type;
    info_ptr->rowbytes = png_ptr->info_store.rowbytes;
}

static inline png_uint_32 png_get_image_width(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->width : 0;
}

static inline png_uint_32 png_get_image_height(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->height : 0;
}

static inline png_uint_32 png_get_rowbytes(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->rowbytes : 0;
}

static inline int png_get_color_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->color_type : PNG_COLOR_TYPE_GRAY;
}

static inline int png_get_bit_depth(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->bit_depth : 8;
}

static inline int png_get_filter_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->filter_type : PNG_FILTER_TYPE_DEFAULT;
}

static inline int png_get_compression_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->compression_type : PNG_COMPRESSION_TYPE_DEFAULT;
}

static inline int png_get_interlace_type(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    return info_ptr ? info_ptr->interlace_type : PNG_INTERLACE_NONE;
}

static inline void png_read_row(png_structp png_ptr, png_bytep row, png_bytep display_row)
{
    (void)display_row;
    if (!png_ptr || !row)
        return;
    size_t count = png_ptr->info_store.rowbytes;
    if (count == 0)
        count = png_ptr->info_store.width;
    memset(row, 0, count);
}

static inline void png_read_end(png_structp png_ptr, png_infop info_ptr)
{
    (void)png_ptr;
    (void)info_ptr;
}

static inline void png_init_io(png_structp png_ptr, void *fp)
{
    if (!png_ptr)
        return;
    png_ptr->write_io_ptr = fp;
}

static inline void png_set_rows(png_structp png_ptr, png_infop info_ptr, png_bytepp rows)
{
    (void)png_ptr;
    if (info_ptr)
        info_ptr->rows = rows;
}

static inline void png_write_png(png_structp png_ptr, png_infop info_ptr, int transforms, png_voidp params)
{
    (void)png_ptr; (void)info_ptr; (void)transforms; (void)params;
}

static inline png_voidp png_malloc(png_structp png_ptr, png_size_t size)
{
    (void)png_ptr;
    return malloc(size);
}

static inline void png_free(png_structp png_ptr, png_voidp ptr)
{
    (void)png_ptr;
    free(ptr);
}

static inline png_voidp png_get_io_ptr(png_structp png_ptr)
{
    return png_ptr ? png_ptr->io_ptr : NULL;
}

static inline int png_sig_cmp(png_const_bytep sig, png_size_t start, png_size_t num_to_check)
{
    (void)sig; (void)start;
    return (num_to_check >= 8) ? 0 : 1;
}

static inline int png_check_sig(png_const_bytep sig, png_size_t num_to_check)
{
    (void)sig;
    return (num_to_check >= 8) ? 1 : 0;
}

static inline int png_set_IHDR(png_structp png_ptr, png_infop info_ptr, png_uint_32 width,
                               png_uint_32 height, int bit_depth, int color_type, int interlace_type,
                               int compression_type, int filter_type)
{
    if (!png_ptr || !info_ptr)
        return 0;
    png_ptr->info_store.width = width;
    png_ptr->info_store.height = height;
    png_ptr->info_store.bit_depth = bit_depth;
    png_ptr->info_store.color_type = color_type;
    png_ptr->info_store.interlace_type = interlace_type;
    png_ptr->info_store.compression_type = compression_type;
    png_ptr->info_store.filter_type = filter_type;
    png_ptr->info_store.rowbytes = (png_size_t)(width * ((color_type == PNG_COLOR_TYPE_RGB) ? 3 : (color_type == PNG_COLOR_TYPE_RGB_ALPHA ? 4 : 1)));
    if (png_ptr->info_store.rowbytes == 0)
        png_ptr->info_store.rowbytes = width;

    info_ptr->width = width;
    info_ptr->height = height;
    info_ptr->bit_depth = bit_depth;
    info_ptr->color_type = color_type;
    info_ptr->interlace_type = interlace_type;
    info_ptr->compression_type = compression_type;
    info_ptr->filter_type = filter_type;
    info_ptr->rowbytes = png_ptr->info_store.rowbytes;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* PNG_H */
