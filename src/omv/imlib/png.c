/*
 * This file is part of the OpenMV project.
 *
 * Copyright (c) 2013-2021 Ibrahim Abdelkader <iabdalkader@openmv.io>
 * Copyright (c) 2013-2021 Kwabena W. Agyeman <kwagyeman@openmv.io>
 *
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * PNG CODEC
 */
#include <stdio.h>

#include "ff_wrapper.h"
#include "imlib.h"
#include "omv_boardconfig.h"
#include "lodepng.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "umm_malloc.h"

#define TIME_PNG   (1)
#define FB_ALLOC_PADDING (1024)

void* lodepng_malloc(size_t size)
{
    return umm_malloc(size);
}

void* lodepng_realloc(void* ptr, size_t new_size)
{
    return umm_realloc(ptr, new_size);
}

void lodepng_free(void* ptr)
{
    return umm_free(ptr);
}

bool png_compress(image_t *src, image_t *dst)
{
    #if (TIME_PNG==1)
    mp_uint_t start = mp_hal_ticks_ms();
    #endif

    if (src->is_compressed) {
        return true;
    }

    umm_init_x(fb_avail());

    LodePNGState state;
    lodepng_state_init(&state);

    switch (src->pixfmt) {
        case PIXFORMAT_BINARY:
            state.info_raw.bitdepth = 1;
            state.info_raw.colortype = LCT_GREY;

            state.encoder.auto_convert = false;
            state.info_png.color.bitdepth = 8;
            state.info_png.color.colortype = LCT_GREY;
            break;
        case PIXFORMAT_GRAYSCALE:
            state.info_raw.bitdepth = 8;
            state.info_raw.colortype = LCT_GREY;

            state.encoder.auto_convert = false;
            state.info_png.color.bitdepth = 8;
            state.info_png.color.colortype = LCT_GREY;
            break;
        case PIXFORMAT_RGB565:
            state.info_raw.bitdepth = 16;
            state.info_raw.colortype = LCT_RGB565;

            state.encoder.auto_convert = false;
            state.info_png.color.bitdepth = 8;
            state.info_png.color.colortype = LCT_RGB;
            break;
        case PIXFORMAT_YUV_ANY:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Input format is not supported"));
            break;
        case PIXFORMAT_BAYER_ANY:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Input format is not supported"));
            break;
    }

    size_t   png_size = 0;
    uint8_t *png_data = NULL;
    unsigned error = lodepng_encode(&png_data, &png_size, src->data, src->w, src->h, &state);
    lodepng_state_cleanup(&state);
    if (error) {
        mp_raise_msg(&mp_type_RuntimeError, (mp_rom_error_text_t) lodepng_error_text(error));
    }

    if (dst->data == NULL) {
        dst->data = png_data;
        dst->size = png_size;
        // fb_alloc() memory ill be free'd by called.
    } else {
        if (image_size(dst) <= png_size) {
            dst->size = png_size;
            memcpy(dst->data, png_data, png_size);
        } else {
            mp_raise_msg_varg(&mp_type_RuntimeError,
                    MP_ERROR_TEXT("Failed to compress image in place"));
        }
        // free fb_alloc() memory used for umm_init_x().
        fb_free(); // umm_init_x();
    }

    #if (TIME_PNG==1)
    printf("time: %u ms\n", mp_hal_ticks_ms() - start);
    #endif

    return false;
}

void png_decompress(image_t *dst, image_t *src)
{
    #if (TIME_PNG==1)
    mp_uint_t start = mp_hal_ticks_ms();
    #endif

    umm_init_x(fb_avail());

    LodePNGState state;
    lodepng_state_init(&state);

    switch (dst->pixfmt) {
        case PIXFORMAT_BINARY:
            state.info_raw.bitdepth = 1;
            state.info_raw.colortype = LCT_GREY;
            break;
        case PIXFORMAT_GRAYSCALE:
            state.info_raw.bitdepth = 8;
            state.info_raw.colortype = LCT_GREY;
            break;
        case PIXFORMAT_RGB565:
            state.info_raw.bitdepth = 16;
            state.info_raw.colortype = LCT_RGB565;

            state.encoder.auto_convert = false;
            state.info_png.color.bitdepth = 8;
            state.info_png.color.colortype = LCT_RGB;
            break;
        case PIXFORMAT_YUV_ANY:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Input format is not supported"));
            break;
        case PIXFORMAT_BAYER_ANY:
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Input format is not supported"));
            break;
    }

    uint8_t *png_data = NULL;
    uint32_t img_size = image_size(dst);
    unsigned error = lodepng_decode(&png_data, (unsigned *) &dst->w, (unsigned *) &dst->h, &state, src->data, src->size);
    lodepng_state_cleanup(&state);
    if (error) {
        mp_raise_msg(&mp_type_RuntimeError, (mp_rom_error_text_t) lodepng_error_text(error));
    }

    uint32_t new_img_size = image_size(dst);
    if (new_img_size <= img_size) {
        memcpy(dst->data, png_data, new_img_size);
    } else {
        mp_raise_msg_varg(&mp_type_RuntimeError,
                MP_ERROR_TEXT("Failed to compress image in place"));
    }

    // free fb_alloc() memory used for umm_init_x().
    fb_free(); // umm_init_x();

    #if (TIME_PNG==1)
    printf("time: %u ms\n", mp_hal_ticks_ms() - start);
    #endif
}

#if defined(IMLIB_ENABLE_IMAGE_FILE_IO)
// This function inits the geometry values of an image.
void png_read_geometry(FIL *fp, image_t *img, const char *path, png_read_settings_t *rs)
{
    uint32_t header;
    file_seek(fp, 12); // start of IHDR
    read_long(fp, &header);
    if (header == 0x52444849) { // IHDR
        uint32_t width, height;
        read_long(fp, &width);
        read_long(fp, &height);
        width = __builtin_bswap32(width);
        height = __builtin_bswap32(height);

        rs->png_w = width;
        rs->png_h = height;
        rs->png_size = IMLIB_IMAGE_MAX_SIZE(f_size(fp));

        img->w = rs->png_w;
        img->h = rs->png_h;
        img->size = rs->png_size;
        img->pixfmt = PIXFORMAT_PNG;
    } else {
        ff_file_corrupted(fp);
    }
}

// This function reads the pixel values of an image.
void png_read_pixels(FIL *fp, image_t *img)
{
    file_seek(fp, 0);
    read_data(fp, img->pixels, img->size);
}

void png_read(image_t *img, const char *path)
{
    FIL fp;
    png_read_settings_t rs;

    file_read_open(&fp, path);

    // Do not use file_buffer_on() here.
    png_read_geometry(&fp, img, path, &rs);

    if (!img->pixels) {
        img->pixels = xalloc(img->size);
    }

    png_read_pixels(&fp, img);
    file_close(&fp);
}

void png_write(image_t *img, const char *path)
{
    FIL fp;
    file_write_open(&fp, path);
    if (img->pixfmt == PIXFORMAT_PNG) {
        write_data(&fp, img->pixels, img->size);
    } else {
        image_t out = { .w=img->w, .h=img->h, .pixfmt=PIXFORMAT_PNG, .size=0, .pixels=NULL }; // alloc in png compress
        png_compress(img, &out);
        write_data(&fp, out.pixels, out.size);
        fb_free(); // frees alloc in png_compress()
    }
    file_close(&fp);
}
#endif //IMLIB_ENABLE_IMAGE_FILE_IO)
