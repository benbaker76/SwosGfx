/*
 * pal_file.c -- palette files for paint programs; see pal_file.h.
 *
 * PalFile.cs's WritePalette and its five writers, in the C#'s order and with
 * its names. Each writer builds the file in memory and writes it whole, as the
 * C# does with File.WriteAllBytes / File.WriteAllLines.
 */
#include "pal_file.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_util.h"

#define EOL "\r\n"      /* Environment.NewLine of the Windows reference */

const char *const Sg_PalFormatNames[SG_PAL_FORMAT_COUNT] = {
    "act", "mspal", "jasc", "gimp", "paintnet"
};

const char *Sg_PalFormatExtension(SgPalFormat format)
{
    switch (format) {
    case SG_PAL_FORMAT_ACT:      return ".act";
    case SG_PAL_FORMAT_MSPAL:    return ".pal";
    case SG_PAL_FORMAT_JASC:     return ".pal";
    case SG_PAL_FORMAT_GIMP:     return ".gpl";
    case SG_PAL_FORMAT_PAINTNET: return ".txt";
    default:                     return NULL;
    }
}

/* ---- a growing byte list (the C#'s List<byte> / List<string>) ------------ */

typedef struct {
    uint8_t  *data;
    uint32_t  size, cap;
    int32_t   failed;
} PalBytes;

static void pb_add(PalBytes *b, const void *p, uint32_t n)
{
    if (b->failed)
        return;
    if (b->size + n > b->cap) {
        uint32_t cap = b->cap ? b->cap * 2 : 1024;
        while (cap < b->size + n)
            cap *= 2;
        uint8_t *d = (uint8_t *)realloc(b->data, cap);
        if (!d) {
            b->failed = 1;
            return;
        }
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->size, p, n);
    b->size += n;
}

static void pb_byte(PalBytes *b, uint8_t v) { pb_add(b, &v, 1); }

/* BitConverter.GetBytes(int / short): little-endian. */
static void pb_le32(PalBytes *b, int32_t v)
{
    uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    pb_add(b, t, 4);
}

static void pb_le16(PalBytes *b, int16_t v)
{
    uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    pb_add(b, t, 2);
}

/* One line of File.WriteAllLines. */
static void pb_line(PalBytes *b, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof line) {
        b->failed = 1;
        return;
    }
    pb_add(b, line, (uint32_t)n);
    pb_add(b, EOL, 2);
}

static int32_t pb_write(PalBytes *b, const char *fileName, char *err)
{
    int32_t rc;
    if (b->failed) {
        snprintf(err, 256, "out of memory writing %.200s", fileName);
        rc = -1;
    } else {
        rc = Sg_WriteFile(fileName, b->data, b->size, err);
    }
    free(b->data);
    return rc;
}

/* Path.GetFileNameWithoutExtension. */
static void file_name_without_extension(const char *path, char *out, uint32_t cap)
{
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    const char *dot = strrchr(name, '.');
    size_t len = dot ? (size_t)(dot - name) : strlen(name);
    if (len >= cap)
        len = cap - 1;
    memcpy(out, name, len);
    out[len] = '\0';
}

#define R_OF(c) ((uint8_t)((c) >> 16))
#define G_OF(c) ((uint8_t)((c) >> 8))
#define B_OF(c) ((uint8_t)(c))

/* ---- the writers --------------------------------------------------------- */

/* 256 RGB triples, padded with black; a palette of fewer colours (or one with a
 * transparent entry) adds the count and the transparent index, big-endian, with
 * 0xFFFF for none. */
static int32_t WriteActFile(const char *fileName, const uint32_t *pal, int32_t count,
                            int32_t transparentIndex, char *err)
{
    PalBytes b = { 0 };

    for (int32_t i = 0; i < 256; i++) {
        if (i < count) {
            pb_byte(&b, R_OF(pal[i]));
            pb_byte(&b, G_OF(pal[i]));
            pb_byte(&b, B_OF(pal[i]));
        } else {
            pb_byte(&b, 0);
            pb_byte(&b, 0);
            pb_byte(&b, 0);
        }
    }

    if (transparentIndex != -1 || count < 256) {
        pb_byte(&b, (uint8_t)(count >> 8));
        pb_byte(&b, (uint8_t)count);

        if (transparentIndex == -1) {
            pb_byte(&b, 0xFF);
            pb_byte(&b, 0xFF);
        } else {
            pb_byte(&b, (uint8_t)(transparentIndex >> 8));
            pb_byte(&b, (uint8_t)transparentIndex);
        }
    }

    return pb_write(&b, fileName, err);
}

/* RIFF "PAL " with one "data" chunk: version 0x0300 (stored 00 03), a count,
 * then RGB0 per colour. The count field is always 256, whatever the number of
 * entries that follow -- the C# writes (short)256; kept so the files match. */
static int32_t WriteMSPalFile(const char *fileName, const uint32_t *pal, int32_t count, char *err)
{
    PalBytes b = { 0 };

    pb_add(&b, "RIFF", 4);
    pb_le32(&b, count * 4 + 16);
    pb_add(&b, "PAL ", 4);
    pb_add(&b, "data", 4);
    pb_le32(&b, count * 4 + 4);
    pb_byte(&b, 0x00);
    pb_byte(&b, 0x03);
    pb_le16(&b, 256);

    for (int32_t i = 0; i < count; i++) {
        pb_byte(&b, R_OF(pal[i]));
        pb_byte(&b, G_OF(pal[i]));
        pb_byte(&b, B_OF(pal[i]));
        pb_byte(&b, 0);
    }

    return pb_write(&b, fileName, err);
}

static int32_t WriteJASCPalFile(const char *fileName, const uint32_t *pal, int32_t count, char *err)
{
    PalBytes b = { 0 };

    pb_line(&b, "JASC-PAL");
    pb_line(&b, "0100");
    pb_line(&b, "%d", (int)count);

    for (int32_t i = 0; i < count; i++)
        pb_line(&b, "%u %u %u", R_OF(pal[i]), G_OF(pal[i]), B_OF(pal[i]));

    return pb_write(&b, fileName, err);
}

static int32_t WriteGIMPPalFile(const char *fileName, const uint32_t *pal, int32_t count, char *err)
{
    PalBytes b = { 0 };
    char name[256];

    file_name_without_extension(fileName, name, sizeof name);

    pb_line(&b, "GIMP Palette");
    pb_line(&b, "Name: %s", name);
    pb_line(&b, "Columns: 0");
    pb_line(&b, "#");

    /* "{0,3} {1,3} {2,3}\tUntitled": right-aligned in three columns. */
    for (int32_t i = 0; i < count; i++)
        pb_line(&b, "%3u %3u %3u\tUntitled", R_OF(pal[i]), G_OF(pal[i]), B_OF(pal[i]));

    return pb_write(&b, fileName, err);
}

static int32_t WritePaintNETPalFile(const char *fileName, const uint32_t *pal, int32_t count, char *err)
{
    PalBytes b = { 0 };
    char name[256];

    file_name_without_extension(fileName, name, sizeof name);

    pb_line(&b, "; Paint.NET Palette");
    pb_line(&b, "; %s", name);

    /* Color.ToArgb().ToString("X8"): the alpha too. */
    for (int32_t i = 0; i < count; i++)
        pb_line(&b, "%08X", (unsigned)pal[i]);

    return pb_write(&b, fileName, err);
}

int32_t Sg_PalFileWrite(const char *fileName, const uint32_t *argb, int32_t count,
                        int32_t transparentIndex, SgPalFormat format, char *err)
{
    switch (format) {
    case SG_PAL_FORMAT_ACT:      return WriteActFile(fileName, argb, count, transparentIndex, err);
    case SG_PAL_FORMAT_MSPAL:    return WriteMSPalFile(fileName, argb, count, err);
    case SG_PAL_FORMAT_JASC:     return WriteJASCPalFile(fileName, argb, count, err);
    case SG_PAL_FORMAT_GIMP:     return WriteGIMPPalFile(fileName, argb, count, err);
    case SG_PAL_FORMAT_PAINTNET: return WritePaintNETPalFile(fileName, argb, count, err);
    default:
        snprintf(err, 256, "unsupported palette format %d", (int)format);
        return -1;
    }
}
