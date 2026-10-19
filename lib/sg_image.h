/*
 * sg_image.h -- an 8-bit indexed picture and its palette, and BMP files of one.
 *
 * Every converter in swosdata works in this form: the game's banks, pitches and
 * pictures are all palette indices, and the palette travels beside them.
 */
#ifndef SWOSGFX_IMAGE_H
#define SWOSGFX_IMAGE_H

#include <stdint.h>

typedef struct {
    int32_t  w, h;
    uint8_t *px;            /* w*h indices, top row first */
    uint8_t  pal[256][3];   /* R, G, B */
} SgImage;

/* A new picture of w x h, all index 0, palette black. 0 on success. */
int32_t  SgImage_Init(SgImage *img, int32_t w, int32_t h);
void     SgImage_Free(SgImage *img);

/* Set the palette from 0x00RRGGBB words (the form SwosGfx keeps them in). */
void     SgImage_SetPaletteArgb(SgImage *img, const uint32_t *argb, int32_t count);

/* 8bpp BMP, bottom-up, 256-entry palette: SwosGfx's layout, so a file written
 * here is byte-for-byte what SwosGfx wrote for the same picture. */
int32_t  SgImage_WriteBmp(const SgImage *img, const char *path, char *err);
/* Read an 8bpp (or 4bpp) BMP, either row order. */
int32_t  SgImage_ReadBmp(SgImage *img, const char *path, char *err);

#endif
