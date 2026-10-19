/*
 * sd_image.c -- an 8-bit indexed picture and its palette, and BMP files of one.
 */
#include "sg_image.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t SgImage_Init(SgImage *img, int32_t w, int32_t h)
{
    memset(img, 0, sizeof *img);
    if (w <= 0 || h <= 0)
        return -1;
    img->px = calloc((size_t)w * (size_t)h, 1);
    if (!img->px)
        return -1;
    img->w = w;
    img->h = h;
    return 0;
}

void SgImage_Free(SgImage *img)
{
    free(img->px);
    img->px = NULL;
    img->w = img->h = 0;
}

void SgImage_SetPaletteArgb(SgImage *img, const uint32_t *argb, int32_t count)
{
    for (int32_t i = 0; i < 256; i++) {
        uint32_t c = i < count ? argb[i] : 0;
        img->pal[i][0] = (uint8_t)(c >> 16);
        img->pal[i][1] = (uint8_t)(c >> 8);
        img->pal[i][2] = (uint8_t)c;
    }
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int32_t SgImage_WriteBmp(const SgImage *img, const char *path, char *err)
{
    uint32_t row = ((uint32_t)img->w + 3) & ~3u;
    uint32_t imageSize = row * (uint32_t)img->h;
    uint32_t pixelOffset = 14 + 40 + 256 * 4;
    uint32_t fileSize = pixelOffset + imageSize;
    uint8_t *f = calloc(1, fileSize);

    put16(f + 0, 0x4D42);
    put32(f + 2, fileSize);
    put32(f + 10, pixelOffset);
    put32(f + 14, 40);
    put32(f + 18, (uint32_t)img->w);
    put32(f + 22, (uint32_t)img->h);
    put16(f + 26, 1);
    put16(f + 28, 8);
    put32(f + 34, imageSize);
    put32(f + 46, 256);
    for (int32_t i = 0; i < 256; i++) {
        uint8_t *e = f + 54 + i * 4;
        e[0] = img->pal[i][2];
        e[1] = img->pal[i][1];
        e[2] = img->pal[i][0];
    }
    for (int32_t y = 0; y < img->h; y++)
        memcpy(f + pixelOffset + (uint32_t)(img->h - 1 - y) * row, img->px + (size_t)y * img->w, (size_t)img->w);

    int32_t rc = Sg_WriteFile(path, f, fileSize, err);
    free(f);
    return rc;
}

int32_t SgImage_ReadBmp(SgImage *img, const char *path, char *err)
{
    SgBuf b;
    memset(img, 0, sizeof *img);
    if (Sg_ReadFile(path, &b, err) != 0)
        return -1;
    const uint8_t *d = b.data;
    if (b.size < 54 || d[0] != 'B' || d[1] != 'M') {
        Sg_BufFree(&b);
        snprintf(err, 256, "%s is not a BMP", path);
        return -1;
    }
    uint32_t offset = Sg_Le32(d + 10);
    uint32_t hdr = Sg_Le32(d + 14);
    int32_t w = (int32_t)Sg_Le32(d + 18);
    int32_t h = (int32_t)Sg_Le32(d + 22);
    uint16_t bpp = Sg_Le16(d + 28);
    uint32_t comp = Sg_Le32(d + 30);
    uint32_t used = Sg_Le32(d + 46);
    int32_t topDown = h < 0;
    if (topDown)
        h = -h;
    if ((bpp != 8 && bpp != 4) || comp != 0 || w <= 0 || h <= 0) {
        Sg_BufFree(&b);
        snprintf(err, 256, "%s: only uncompressed 4- or 8-bit BMPs are read", path);
        return -1;
    }
    uint32_t colors = used ? used : (1u << bpp);
    uint32_t row = (((uint32_t)w * bpp + 31) / 32) * 4;
    if (14 + hdr + colors * 4 > b.size || offset + row * (uint32_t)h > b.size) {
        Sg_BufFree(&b);
        snprintf(err, 256, "%s is cut short", path);
        return -1;
    }
    SgImage_Init(img, w, h);
    for (uint32_t i = 0; i < colors && i < 256; i++) {
        const uint8_t *e = d + 14 + hdr + i * 4;
        img->pal[i][0] = e[2];
        img->pal[i][1] = e[1];
        img->pal[i][2] = e[0];
    }
    for (int32_t y = 0; y < h; y++) {
        const uint8_t *src = d + offset + (uint32_t)(topDown ? y : h - 1 - y) * row;
        uint8_t *dst = img->px + (size_t)y * w;
        if (bpp == 8)
            memcpy(dst, src, (size_t)w);
        else
            for (int32_t x = 0; x < w; x++)
                dst[x] = (x & 1) ? (src[x >> 1] & 0x0F) : (src[x >> 1] >> 4);
    }
    Sg_BufFree(&b);
    return 0;
}
