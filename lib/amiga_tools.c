/*
 * amiga_tools.c -- SwosGfx's Amiga modes (see amiga_tools.h).
 *
 * A line-for-line port of AmigaTools.cs, AmigaRncHelper.cs and Program.cs
 * RunAmiga, keeping the C# names. What is shared with the rest of the library:
 *   - RAW planar decode/encode and the RAW sizes are amiga_raw.c's
 *     (Sg_AmigaRawDecode / Sg_AmigaRawEncode / Sg_AmigaRawDimensions), which
 *     were ported from the same C# methods and lay the planes out identically;
 *     only the C#'s checks and messages around them are added here.
 *   - QuantizeBmp is amiga_palette.c's (Sg_AmigaQuantizeToPalette / ToCount),
 *     not amiga_raw.c's Sg_AmigaRemapToPalette: that one silently skips an
 *     index past the old palette where the C# throws.
 *   - BMP files are sg_image.c's, whose writer is the C#'s Bmp8Helper byte for
 *     byte; its reader is more lenient than Bmp8Helper's (it takes top-down
 *     BMPs), so the C#'s header checks are made first (ReadIndexedBmp).
 *   - The 16-pixel tile rows of a MAP are encoded/decoded here (EncodeAmigaRow16
 *     / DecodeAmigaRow16): they are a RAW row of width 16, but the C# keeps them
 *     separate and they are short.
 */
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "amiga_tools.h"

#include "amiga_palette.h"
#include "amiga_raw.h"
#include "dos_palette.h"
#include "rnc_pack.h"
#include "sg_dir.h"
#include "sg_image.h"
#include "sg_util.h"
#include "tiled_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* Pitch constants */
#define PitchTilesX            SG_AMIGA_PITCH_TILES_X
#define PitchTilesY            SG_AMIGA_PITCH_TILES_Y
#define TileSize               SG_AMIGA_PITCH_TILE
#define PitchWidth             (PitchTilesX * TileSize)        /* 672 */
#define PitchHeight            (PitchTilesY * TileSize)        /* 880 */
#define PitchCells             (PitchTilesX * PitchTilesY)

/* MAP layout constants */
#define MapHeaderSize          (PitchCells * 4)                /* 9240 */
#define SpriteRows             16
#define BytesPerPlanePerRow16  (TileSize / 8)                  /* 2 */

/* Default number of columns for the tileset image in pitch-map-to-tiled */
#define TileSheetColumns       16

/* A 16x16 tile, [y * 16 + x] (PitchBitmapItem.Bits[y,x]). */
typedef struct {
    uint8_t bits[TileSize * TileSize];
} PitchBitmapItem;

static void Log(const SgAmigaTools *t, const char *line)
{
    if (t && t->log)
        t->log(t->logUser, line);
}

/* ------------------------------------------------------------------------
 * AmigaRncHelper
 * ------------------------------------------------------------------------ */

/* IsRnc: just the three letters, as the C# checks (Sg_IsRnc also wants a
 * method and a whole header; a file that has the letters and not those is a
 * failed unpack in the C#, and is one here). */
static int32_t IsRnc(const SgBuf *b)
{
    return b->size >= 4 && b->data[0] == 'R' && b->data[1] == 'N' && b->data[2] == 'C';
}

/* ReadAllBytes: the file, unpacked when it is RNC. */
static int32_t RncReadAllBytes(const char *path, SgBuf *out, char *err)
{
    SgBuf data;
    char why[256];

    out->data = NULL;
    out->size = 0;
    if (Sg_ReadFile(path, &data, err) != 0)
        return -1;
    if (!IsRnc(&data)) {
        *out = data;
        return 0;
    }
    if (!Sg_IsRnc(&data) || Sg_RncUnpack(&data, out, why) != 0) {
        if (!Sg_IsRnc(&data))
            snprintf(why, sizeof why, "not a valid RNC header");
        snprintf(err, 256, "RNC unpack failed (%.100s): %.120s", why, path);
        Sg_BufFree(&data);
        Sg_BufFree(out);
        return -1;
    }
    Sg_BufFree(&data);
    return 0;
}

/* WriteAllBytes: packed (method 2) unless -no-rnc, and never packed twice. */
static int32_t RncWriteAllBytes(const SgAmigaTools *t, const char *path,
                                const uint8_t *data, uint32_t size, char *err)
{
    SgBuf raw, packed;
    int32_t rc;

    raw.data = (uint8_t *)data;
    raw.size = size;
    if (!t->writeRnc || IsRnc(&raw))
        return Sg_WriteFile(path, data, size, err);

    if (Sg_RncPackLikeSwosGfx(&raw, &packed, err) != 0) {
        char why[256];
        snprintf(why, sizeof why, "%s", err);
        snprintf(err, 256, "RNC pack failed (%.200s).", why);
        return -1;
    }
    rc = Sg_WriteFile(path, packed.data, packed.size, err);
    Sg_BufFree(&packed);
    return rc;
}

/* ------------------------------------------------------------------------
 * Planar helpers for 16-pixel rows (1-8 bitplanes)
 * ------------------------------------------------------------------------ */

/* Encode 16 pixels into bitplanes * 2 planar bytes. An index too big for the
 * depth fails, as the C# throws. */
static int32_t EncodeAmigaRow16(const uint8_t *inputPixels16, int32_t bitplanes,
                                uint8_t *outputRowBytes, char *err)
{
    int32_t maxIndex = (1 << bitplanes) - 1;
    int32_t outIndex = 0;

    for (int32_t plane = 0; plane < bitplanes; plane++) {
        for (int32_t block = 0; block < BytesPerPlanePerRow16; block++) {
            uint8_t value = 0;
            /* Each byte packs 8 pixels of this plane, MSB first. */
            for (int32_t bit = 0; bit < 8; bit++) {
                int32_t pix = inputPixels16[block * 8 + bit];
                if (pix > maxIndex) {
                    snprintf(err, 256, "Pixel index %d exceeds maximum %d for %d bitplanes.",
                             (int)pix, (int)maxIndex, (int)bitplanes);
                    return -1;
                }
                value = (uint8_t)((value << 1) | ((pix >> plane) & 1));  /* plane 0 = LSB */
            }
            outputRowBytes[outIndex++] = value;
        }
    }
    return 0;
}

static void DecodeAmigaRow16(const uint8_t *inputRowBytes, int32_t bitplanes, uint8_t *outputPixels16)
{
    for (int32_t x = 0; x < TileSize; x++) {
        int32_t pixelIndex = 0;
        int32_t byteIndexInPlane = x / 8;
        int32_t bitIndexInByte = 7 - (x % 8);   /* MSB first */

        for (int32_t plane = 0; plane < bitplanes; plane++) {
            uint8_t b = inputRowBytes[plane * BytesPerPlanePerRow16 + byteIndexInPlane];
            pixelIndex |= ((b >> bitIndexInByte) & 1) << plane;
        }
        outputPixels16[x] = (uint8_t)pixelIndex;
    }
}

/* ------------------------------------------------------------------------
 * BMP (Bmp8Helper)
 * ------------------------------------------------------------------------ */

/* Read8bppBmpToIndices: Bmp8Helper's checks, in its order and with its
 * messages, then sg_image.c's reader. The palette is not returned: no caller
 * of it in AmigaTools uses it. */
static int32_t ReadIndexedBmp(const char *path, SgImage *img, char *err)
{
    SgBuf b;
    const uint8_t *d;

    memset(img, 0, sizeof *img);
    if (Sg_ReadFile(path, &b, err) != 0)
        return -1;
    d = b.data;
    if (b.size < 2 || Sg_Le16(d) != 0x4D42) {
        snprintf(err, 256, "Not a BMP file (missing 'BM').");
        goto fail;
    }
    if (b.size < 54) {
        snprintf(err, 256, "Unable to read beyond the end of the stream.");
        goto fail;
    }
    {
        uint32_t infoSize = Sg_Le32(d + 14);
        int32_t height = (int32_t)Sg_Le32(d + 22);
        uint16_t planes = Sg_Le16(d + 26);
        uint16_t bitCount = Sg_Le16(d + 28);
        uint32_t compression = Sg_Le32(d + 30);

        if (infoSize < 40) {
            snprintf(err, 256, "Unsupported BMP info header size.");
            goto fail;
        }
        if (bitCount != 4 && bitCount != 8) {
            snprintf(err, 256, "Only 4bpp or 8bpp indexed BMPs are supported.");
            goto fail;
        }
        if (compression != 0) {
            snprintf(err, 256, "Compressed BMPs are not supported.");
            goto fail;
        }
        if (planes != 1) {
            snprintf(err, 256, "Invalid planes count in BMP (expected 1).");
            goto fail;
        }
        if (height <= 0) {
            /* The C# takes height 0 as top-down too (bottomUp = height > 0). */
            snprintf(err, 256, "Top-down BMPs (negative height) are not supported here.");
            goto fail;
        }
    }
    Sg_BufFree(&b);
    return SgImage_ReadBmp(img, path, err);

fail:
    Sg_BufFree(&b);
    return -1;
}

/* Write8bppBmpFromIndices: quantize (onto newPalette, or down to colorCount
 * entries), then the 8bpp BMP. `indices` (w*h) is changed in place, as the C#'s
 * `ref` array is. */
static int32_t Write8bppBmpFromIndices(const char *path, uint8_t *indices, int32_t w, int32_t h,
                                       const uint32_t *palette, int32_t paletteCount,
                                       const uint32_t *newPalette, int32_t newCount,
                                       int32_t colorCount, char *err)
{
    SgImage img;
    const uint32_t *finalPal;
    int32_t finalCount;

    if (newPalette && newCount > 0) {
        if (Sg_AmigaQuantizeToPalette(indices, w, h, palette, paletteCount, newPalette, newCount, err) != 0)
            return -1;
        finalPal = newPalette;
        finalCount = newCount;
    } else {
        if (Sg_AmigaQuantizeToCount(indices, w, h, palette, paletteCount, colorCount, err) != 0) {
            /* ArgumentOutOfRangeException's own suffix, as SwosGfx prints it */
            if (strncmp(err, "colorCount must", 15) == 0 && strlen(err) < 200)
                strcat(err, " (Parameter 'colorCount')");
            return -1;
        }
        finalPal = palette;
        finalCount = colorCount;
    }

    memset(&img, 0, sizeof img);
    img.w = w;
    img.h = h;
    img.px = indices;
    SgImage_SetPaletteArgb(&img, finalPal, finalCount);
    return SgImage_WriteBmp(&img, path, err);
}

/* ------------------------------------------------------------------------
 * MAP <-> sprites + mapping
 * ------------------------------------------------------------------------ */

typedef struct {
    uint32_t         mapping[PitchCells];   /* the cells' big-endian u32, [y * 42 + x] */
    PitchBitmapItem *sprites;
    int32_t          spriteCount;
} PitchMap;

static void PitchMapFree(PitchMap *m)
{
    free(m->sprites);
    m->sprites = NULL;
    m->spriteCount = 0;
}

static int32_t CheckBitplanes(int32_t bitplanes, char *err)
{
    if (bitplanes < 1 || bitplanes > 8) {
        snprintf(err, 256, "Bitplanes must be between 1 and 8.");
        return -1;
    }
    return 0;
}

static int32_t ReadPitchMap(const char *mapFilePath, int32_t bitplanes, PitchMap *m, char *err)
{
    SgBuf data;
    int32_t spriteRowBytes, spriteSizeBytes, remaining;
    const uint8_t *p;

    memset(m, 0, sizeof *m);
    if (CheckBitplanes(bitplanes, err) != 0)
        return -1;
    if (RncReadAllBytes(mapFilePath, &data, err) != 0)
        return -1;

    if (data.size < MapHeaderSize) {
        snprintf(err, 256, "MAP file too small. Length=%u, expected at least %d.",
                 (unsigned)data.size, MapHeaderSize);
        Sg_BufFree(&data);
        return -1;
    }

    /* 1) Header = mapping */
    for (int32_t i = 0; i < PitchCells; i++)
        m->mapping[i] = Sg_Be32(data.data + i * 4);

    remaining = (int32_t)(data.size - MapHeaderSize);
    spriteRowBytes = BytesPerPlanePerRow16 * bitplanes;
    spriteSizeBytes = SpriteRows * spriteRowBytes;

    if (remaining % spriteSizeBytes != 0) {
        /* The depth is not in the file, so a wrong -bitplanes is the usual
         * cause: say which depths the data would fit. */
        char hint[96] = "";
        int32_t n = 0;
        for (int32_t planes = 1; planes <= 8; planes++) {
            if (remaining % (SpriteRows * BytesPerPlanePerRow16 * planes) == 0) {
                size_t len = strlen(hint);
                snprintf(hint + len, sizeof hint - len, "%s%d", n ? " or " : "", (int)planes);
                n++;
            }
        }
        snprintf(err, 256,
                 "MAP sprite data size (%d bytes) is not a multiple of spriteSizeBytes=%d for %d bitplanes.%s%s%s",
                 (int)remaining, (int)spriteSizeBytes, (int)bitplanes,
                 n ? " The sprite data divides evenly at -bitplanes=" : "", hint, n ? "." : "");
        Sg_BufFree(&data);
        return -1;
    }

    m->spriteCount = remaining / spriteSizeBytes;
    m->sprites = calloc((size_t)(m->spriteCount ? m->spriteCount : 1), sizeof *m->sprites);
    if (!m->sprites) {
        snprintf(err, 256, "out of memory");
        Sg_BufFree(&data);
        return -1;
    }
    p = data.data + MapHeaderSize;
    for (int32_t s = 0; s < m->spriteCount; s++)
        for (int32_t row = 0; row < SpriteRows; row++, p += spriteRowBytes)
            DecodeAmigaRow16(p, bitplanes, m->sprites[s].bits + row * TileSize);

    Sg_BufFree(&data);
    return 0;
}

/* BuildSpritesAndMapping: de-duplicate the 42x55 tiles in reading order (the
 * first of equal tiles is kept) and give each cell its tile's byte offset. */
static int32_t BuildSpritesAndMapping(const SgAmigaTools *t, const PitchBitmapItem *tileMatrix,
                                      int32_t bitplanes, int32_t maxTiles, PitchMap *m, char *err)
{
    int32_t tileIndices[PitchCells];
    int32_t bytesPerTile = 16 * bitplanes * 2;

    memset(m, 0, sizeof *m);
    m->sprites = malloc(PitchCells * sizeof *m->sprites);
    if (!m->sprites) {
        snprintf(err, 256, "out of memory");
        return -1;
    }

    for (int32_t c = 0; c < PitchCells; c++) {
        int32_t foundIndex = -1;
        for (int32_t i = 0; i < m->spriteCount; i++) {
            if (memcmp(tileMatrix[c].bits, m->sprites[i].bits, sizeof tileMatrix[c].bits) == 0) {
                foundIndex = i;
                break;
            }
        }
        if (foundIndex == -1) {
            foundIndex = m->spriteCount;
            m->sprites[m->spriteCount++] = tileMatrix[c];
        }
        tileIndices[c] = foundIndex;
    }

    if (maxTiles > 0 && m->spriteCount > maxTiles) {
        char line[96];
        snprintf(line, sizeof line, "WARNING: Tile count %d exceeds limit %d.",
                 (int)m->spriteCount, (int)maxTiles);
        Log(t, line);
    }

    for (int32_t c = 0; c < PitchCells; c++)
        m->mapping[c] = tileIndices[c] == 0 ? 0u : (uint32_t)(tileIndices[c] * bytesPerTile);
    return 0;
}

static int32_t WritePitchMap(const SgAmigaTools *t, const char *outputMapPath, const PitchMap *m,
                             int32_t bitplanes, char *err)
{
    int32_t spriteRowBytes = BytesPerPlanePerRow16 * bitplanes;
    uint32_t size, at = 0;
    uint8_t *buf;
    int32_t rc;

    if (CheckBitplanes(bitplanes, err) != 0)
        return -1;
    size = MapHeaderSize + (uint32_t)m->spriteCount * (uint32_t)(TileSize * spriteRowBytes);
    buf = malloc(size);
    if (!buf) {
        snprintf(err, 256, "out of memory");
        return -1;
    }

    /* mapping header */
    for (int32_t c = 0; c < PitchCells; c++, at += 4) {
        buf[at] = (uint8_t)(m->mapping[c] >> 24);
        buf[at + 1] = (uint8_t)(m->mapping[c] >> 16);
        buf[at + 2] = (uint8_t)(m->mapping[c] >> 8);
        buf[at + 3] = (uint8_t)m->mapping[c];
    }
    /* sprites: 16 rows of bitplanes * 2 bytes each */
    for (int32_t k = 0; k < m->spriteCount; k++) {
        for (int32_t row = 0; row < TileSize; row++, at += (uint32_t)spriteRowBytes) {
            if (EncodeAmigaRow16(m->sprites[k].bits + row * TileSize, bitplanes, buf + at, err) != 0) {
                free(buf);
                return -1;
            }
        }
    }

    rc = RncWriteAllBytes(t, outputMapPath, buf, size, err);
    free(buf);
    return rc;
}

/* ------------------------------------------------------------------------
 * The mapping offsets
 * ------------------------------------------------------------------------ */

/* DetectPitchOffsetStride: what unit the cells' offsets are counted in. A
 * written MAP holds true byte offsets (tileIndex * 16 * bitplanes * 2), but AGA
 * pitches exist (some made by earlier SwosGfx builds) whose offsets keep the
 * OCS scaling of a fixed 128 bytes a tile, whatever the depth. At 4 bitplanes
 * the two are the same. A stride fits only if every offset is a multiple of it
 * AND lands on a tile that exists, so try the byte stride, then the legacy one. */
static int32_t PitchOffsetsFitStride(const uint32_t *mapping, int32_t stride, int32_t spriteCount)
{
    if (stride <= 0)
        return 0;
    for (int32_t c = 0; c < PitchCells; c++) {
        if (mapping[c] % (uint32_t)stride != 0)
            return 0;
        if (mapping[c] / (uint32_t)stride >= (uint32_t)spriteCount)
            return 0;
    }
    return 1;
}

static int32_t DetectPitchOffsetStride(const uint32_t *mapping, int32_t bitplanes, int32_t spriteCount)
{
    int32_t byteStride = TileSize * bitplanes * 2;   /* what WritePitchMap emits */
    const int32_t LegacyStride = TileSize * 4 * 2;   /* 128: the fixed 4-bitplane tile */

    if (PitchOffsetsFitStride(mapping, byteStride, spriteCount))
        return byteStride;
    if (LegacyStride != byteStride && PitchOffsetsFitStride(mapping, LegacyStride, spriteCount))
        return LegacyStride;
    /* Neither fits: the expected one, so the per-cell decode reports the
     * offending offset rather than silently picking something wrong. */
    return byteStride;
}

static int32_t DecodePitchTileIndex(uint32_t offset, int32_t offsetStride, int32_t *tileIndex, char *err)
{
    if (offset % (uint32_t)offsetStride != 0) {
        snprintf(err, 256, "Bad pitch mapping offset 0x%08X (not divisible by the tile stride of %d bytes).",
                 (unsigned)offset, (int)offsetStride);
        return -1;
    }
    /* (int) of the quotient, as the C#: an offset past 2^31 strides is negative
     * and then fails the caller's range check. */
    *tileIndex = (int32_t)(offset / (uint32_t)offsetStride);
    return 0;
}

/* ------------------------------------------------------------------------
 * 1. PITCH MAP -> FULL PITCH BMP
 * ------------------------------------------------------------------------ */

int32_t Sg_AmigaConvertPitchMapToBmp(const SgAmigaTools *t, const char *mapFilePath,
                                     const uint32_t *overridePalette, int32_t overrideCount,
                                     const char *outputBmpPath,
                                     const uint32_t *newPalette, int32_t newCount,
                                     int32_t bitplanes, int32_t colorCount, char *err)
{
    PitchMap m;
    uint32_t defaultPal[256];
    const uint32_t *palette;
    int32_t paletteCount, offsetStride, rc;
    uint8_t *fullIndices;

    /* 1) mapping + sprites */
    if (ReadPitchMap(mapFilePath, bitplanes, &m, err) != 0)
        return -1;

    /* 2) palette: override or default */
    if (overridePalette) {
        palette = overridePalette;
        paletteCount = overrideCount;
    } else if (bitplanes > 4) {
        palette = Sg_DosPaletteGame;
        paletteCount = 256;
    } else {
        Sg_AmigaPaletteFromAmiga12(Sg_AmigaPaletteGame, 16, defaultPal);
        palette = defaultPal;
        paletteCount = 16;
    }

    /* 3) the full pitch */
    fullIndices = malloc(PitchWidth * PitchHeight);
    if (!fullIndices) {
        PitchMapFree(&m);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    offsetStride = DetectPitchOffsetStride(m.mapping, bitplanes, m.spriteCount);

    for (int32_t y = 0; y < PitchTilesY; y++) {
        for (int32_t x = 0; x < PitchTilesX; x++) {
            int32_t tileIndex;
            if (DecodePitchTileIndex(m.mapping[y * PitchTilesX + x], offsetStride, &tileIndex, err) != 0)
                goto fail;
            if ((uint32_t)tileIndex >= (uint32_t)m.spriteCount) {
                snprintf(err, 256, "Mapping refers to tileIndex %d but sprites.Count=%d.",
                         (int)tileIndex, (int)m.spriteCount);
                goto fail;
            }
            for (int32_t j = 0; j < TileSize; j++)
                memcpy(fullIndices + (size_t)(y * TileSize + j) * PitchWidth + x * TileSize,
                       m.sprites[tileIndex].bits + j * TileSize, TileSize);
        }
    }

    /* 4) the BMP */
    rc = Write8bppBmpFromIndices(outputBmpPath, fullIndices, PitchWidth, PitchHeight,
                                 palette, paletteCount, newPalette, newCount, colorCount, err);
    free(fullIndices);
    PitchMapFree(&m);
    return rc;

fail:
    free(fullIndices);
    PitchMapFree(&m);
    return -1;
}

/* ------------------------------------------------------------------------
 * Paths, as System.IO.Path does them
 * ------------------------------------------------------------------------ */

static int32_t IsSep(char c)
{
    return c == '/' || c == '\\';
}

/* Path.IsPathRooted, for both kinds of path. */
static int32_t IsRooted(const char *p)
{
    return IsSep(p[0]) || (p[0] && p[1] == ':');
}

/* Path.GetDirectoryName, "" for none. */
static void DirName(char *out, uint32_t cap, const char *path)
{
    const char *name = Sg_PathFileName(path);
    size_t n = (size_t)(name - path);

    while (n > 1 && IsSep(path[n - 1]))
        n--;
    if (n == 1 && !IsSep(path[0]))
        n = 0;
    snprintf(out, cap, "%.*s", (int)n, path);
}

/* Path.GetFileNameWithoutExtension */
static void FileNameWithoutExtension(char *out, uint32_t cap, const char *path)
{
    Sg_PathChangeExt(out, cap, path, "");
}

/* Path.Combine(a, b): b alone when it is rooted. */
static void Combine(char *out, uint32_t cap, const char *a, const char *b)
{
    if (!a[0] || IsRooted(b))
        snprintf(out, cap, "%s", b);
    else
        Sg_PathJoin(out, cap, a, b);
}

/* ------------------------------------------------------------------------
 * 2. PITCH MAP -> Tiled map + tileset BMP
 * ------------------------------------------------------------------------ */

int32_t Sg_AmigaConvertPitchMapToTiled(const SgAmigaTools *t, const char *mapFilePath,
                                       const uint32_t *palette, int32_t paletteCount,
                                       const char *dstMapPath,
                                       const uint32_t *newPalette, int32_t newCount,
                                       int32_t bitplanes, int32_t colorCount, char *err)
{
    PitchMap m;
    SgTiledMap tiled;
    char mapDir[1024], baseName[512], bmpName[600], tilesBmpPath[1700];
    int32_t tileCount, destCols, destRows, imgWidth, imgHeight, offsetStride, rc = -1;
    uint8_t *tilesetIndices;

    /* 1) mapping + sprites */
    if (ReadPitchMap(mapFilePath, bitplanes, &m, err) != 0)
        return -1;

    /* 2) the tileset: the sprites in rows of 16 */
    tileCount = m.spriteCount;
    destCols = TileSheetColumns;
    destRows = (tileCount + destCols - 1) / destCols;
    imgWidth = destCols * TileSize;
    imgHeight = destRows * TileSize;
    if (imgHeight == 0) {
        /* The C# would write a BMP of height 0 and then fail on the first cell;
         * a picture of no rows is not something sg_image writes. */
        snprintf(err, 256, "Mapping refers to tileIndex 0 but the MAP holds 0 tiles.");
        PitchMapFree(&m);
        return -1;
    }
    tilesetIndices = calloc((size_t)imgWidth * (size_t)imgHeight, 1);
    if (!tilesetIndices) {
        snprintf(err, 256, "out of memory");
        PitchMapFree(&m);
        return -1;
    }
    for (int32_t s = 0; s < tileCount; s++) {
        int32_t dstX0 = (s % destCols) * TileSize, dstY0 = (s / destCols) * TileSize;
        for (int32_t y = 0; y < TileSize; y++)
            memcpy(tilesetIndices + (size_t)(dstY0 + y) * imgWidth + dstX0, m.sprites[s].bits + y * TileSize,
                   TileSize);
    }

    DirName(mapDir, sizeof mapDir, dstMapPath);
    FileNameWithoutExtension(baseName, sizeof baseName, dstMapPath);
    snprintf(bmpName, sizeof bmpName, "%s.bmp", baseName);
    Combine(tilesBmpPath, sizeof tilesBmpPath, mapDir, bmpName);

    /* 3) the tileset BMP (written before the cells are checked, as the C#) */
    if (Write8bppBmpFromIndices(tilesBmpPath, tilesetIndices, imgWidth, imgHeight, palette, paletteCount,
                                newPalette, newCount, colorCount, err) != 0)
        goto done;

    /* 4) the Tiled map */
    SgTiledMap_Init(&tiled);
    if (SgTiledMap_SetSize(&tiled, PitchTilesX, PitchTilesY, TileSize, TileSize, err) != 0)
        goto done_map;
    offsetStride = DetectPitchOffsetStride(m.mapping, bitplanes, tileCount);
    for (int32_t c = 0; c < PitchCells; c++) {
        int32_t tileIndex;
        if (DecodePitchTileIndex(m.mapping[c], offsetStride, &tileIndex, err) != 0)
            goto done_map;
        if ((uint32_t)tileIndex >= (uint32_t)tileCount) {
            snprintf(err, 256, "Mapping refers to tileIndex %d but the MAP holds %d tiles.",
                     (int)tileIndex, (int)tileCount);
            goto done_map;
        }
        tiled.gids[c] = tileIndex + 1;   /* Tiled's ids are 1-based; 0 is empty */
    }
    tiled.tileCount = tileCount;
    tiled.columns = destCols;
    tiled.imageWidth = imgWidth;
    tiled.imageHeight = imgHeight;
    if (SgTiled_SetString(&tiled.imageSource, bmpName) != 0) {
        snprintf(err, 256, "out of memory");
        goto done_map;
    }
    rc = SgTiledMap_Write(&tiled, dstMapPath, err);

done_map:
    SgTiledMap_Free(&tiled);
done:
    free(tilesetIndices);
    PitchMapFree(&m);
    return rc;
}

/* ------------------------------------------------------------------------
 * 3. Tiled map + tileset BMP -> PITCH MAP
 * ------------------------------------------------------------------------ */

int32_t Sg_AmigaConvertTiledToPitchMap(const SgAmigaTools *t, const char *tiledMapPath,
                                       const char *outputMapPath, int32_t maxTiles,
                                       int32_t bitplanes, char *err)
{
    SgTiledMap tiled;
    SgImage ts;
    PitchBitmapItem *tileLibrary = NULL, *tileMatrix = NULL;
    PitchMap m;
    char mapDir[1024], tilesBmpPath[2048];
    int32_t tileCount, columns, tileSheetRows, rc = -1;

    memset(&m, 0, sizeof m);
    memset(&ts, 0, sizeof ts);
    if (CheckBitplanes(bitplanes, err) != 0)
        return -1;
    if (SgTiledMap_Read(&tiled, tiledMapPath, err) != 0)
        return -1;

    if (tiled.tileWidth != TileSize || tiled.tileHeight != TileSize) {
        snprintf(err, 256, "Tiled map tile size must be %dx%d.", TileSize, TileSize);
        goto done;
    }
    if (tiled.width != PitchTilesX || tiled.height != PitchTilesY) {
        snprintf(err, 256, "Tiled map dimensions must be %dx%d.", PitchTilesX, PitchTilesY);
        goto done;
    }

    tileCount = tiled.tileCount;
    columns = tiled.columns;

    DirName(mapDir, sizeof mapDir, tiledMapPath);
    Combine(tilesBmpPath, sizeof tilesBmpPath, mapDir, tiled.imageSource ? tiled.imageSource : "");

    /* 1) the tileset image */
    if (ReadIndexedBmp(tilesBmpPath, &ts, err) != 0)
        goto done;
    if (ts.w != tiled.imageWidth || ts.h != tiled.imageHeight) {
        snprintf(err, 256, "Tiled map: tileset image dimensions do not match the map's image size.");
        goto done;
    }
    tileSheetRows = tiled.imageHeight / TileSize;

    /* 2) one tile per tileset entry */
    if (tileCount < 0) {
        /* new PitchBitmapItem[negative] */
        snprintf(err, 256, "Arithmetic operation resulted in an overflow.");
        goto done;
    }
    if (tileCount > 0 && columns <= 0) {
        /* tileIndex % 0 in the C#; a negative count of columns lands outside
         * the image there. */
        snprintf(err, 256, columns == 0 ? "Attempted to divide by zero."
                                        : "Index was outside the bounds of the array.");
        goto done;
    }
    tileLibrary = malloc((size_t)(tileCount ? tileCount : 1) * sizeof *tileLibrary);
    if (!tileLibrary) {
        snprintf(err, 256, "out of memory");
        goto done;
    }
    for (int32_t tileIndex = 0; tileIndex < tileCount; tileIndex++) {
        int32_t col = tileIndex % columns, row = tileIndex / columns;
        int32_t srcX0 = col * TileSize, srcY0 = row * TileSize;

        if (row >= tileSheetRows) {
            snprintf(err, 256, "Tiled map: tileset image is too small for the declared tilecount/columns.");
            goto done;
        }
        if (srcX0 + TileSize > ts.w || srcY0 + TileSize > ts.h) {
            /* the C#'s tsIndices[y, x] out of range: more columns than fit */
            snprintf(err, 256, "Index was outside the bounds of the array.");
            goto done;
        }
        for (int32_t y = 0; y < TileSize; y++)
            memcpy(tileLibrary[tileIndex].bits + y * TileSize, ts.px + (size_t)(srcY0 + y) * ts.w + srcX0,
                   TileSize);
    }

    /* 3) the pitch's tiles, from the library and the gids */
    tileMatrix = malloc(PitchCells * sizeof *tileMatrix);
    if (!tileMatrix) {
        snprintf(err, 256, "out of memory");
        goto done;
    }
    for (int32_t c = 0; c < PitchCells; c++) {
        int32_t gid = tiled.gids[c];
        if (gid <= 0) {
            snprintf(err, 256, "Tiled map: tile GID 0 (empty) encountered; SWOS MAP requires all tiles present.");
            goto done;
        }
        if (gid - 1 >= tileCount) {
            snprintf(err, 256, "Tiled map: tile GID %d references out-of-range tile index %d.",
                     (int)gid, (int)(gid - 1));
            goto done;
        }
        tileMatrix[c] = tileLibrary[gid - 1];
    }

    /* 4) de-duplicate, map, write */
    if (BuildSpritesAndMapping(t, tileMatrix, bitplanes, maxTiles, &m, err) != 0)
        goto done;
    rc = WritePitchMap(t, outputMapPath, &m, bitplanes, err);

done:
    PitchMapFree(&m);
    free(tileMatrix);
    free(tileLibrary);
    SgImage_Free(&ts);
    SgTiledMap_Free(&tiled);
    return rc;
}

/* ------------------------------------------------------------------------
 * 4. FULL PITCH BMP -> PITCH MAP
 * ------------------------------------------------------------------------ */

int32_t Sg_AmigaConvertFullPitchBmpToMap(const SgAmigaTools *t, const char *fullPitchBmpPath,
                                         const char *outputMapPath, int32_t maxTiles,
                                         int32_t bitplanes, char *err)
{
    SgImage img;
    PitchBitmapItem *tileMatrix;
    PitchMap m;
    int32_t rc;

    if (CheckBitplanes(bitplanes, err) != 0)
        return -1;

    /* 1) the indices (the palette is not used: they are trusted) */
    if (ReadIndexedBmp(fullPitchBmpPath, &img, err) != 0)
        return -1;
    if (img.w < PitchWidth || img.h < PitchHeight) {
        /* indexBuffer[ty * 16 + j, tx * 16 + i] out of range in the C# */
        snprintf(err, 256, "Index was outside the bounds of the array.");
        SgImage_Free(&img);
        return -1;
    }

    /* 2) slice into 55 x 42 tiles */
    tileMatrix = malloc(PitchCells * sizeof *tileMatrix);
    if (!tileMatrix) {
        snprintf(err, 256, "out of memory");
        SgImage_Free(&img);
        return -1;
    }
    for (int32_t ty = 0; ty < PitchTilesY; ty++)
        for (int32_t tx = 0; tx < PitchTilesX; tx++)
            for (int32_t j = 0; j < TileSize; j++)
                memcpy(tileMatrix[ty * PitchTilesX + tx].bits + j * TileSize,
                       img.px + (size_t)(ty * TileSize + j) * img.w + tx * TileSize, TileSize);
    SgImage_Free(&img);

    /* 3) de-duplicate and map; 4) write at the requested depth */
    rc = BuildSpritesAndMapping(t, tileMatrix, bitplanes, maxTiles, &m, err);
    free(tileMatrix);
    if (rc == 0)
        rc = WritePitchMap(t, outputMapPath, &m, bitplanes, err);
    PitchMapFree(&m);
    return rc;
}

/* ------------------------------------------------------------------------
 * 5. RAW <-> BMP
 * ------------------------------------------------------------------------ */

int32_t Sg_AmigaConvertBmpToRaw(const SgAmigaTools *t, const char *inputBmpPath,
                                const char *outputRawPath, int32_t bitplanes, char *err)
{
    SgImage img;
    SgBuf raw;
    int32_t autoBitplanes = bitplanes <= 0, maxIndex, rc;
    uint32_t n;

    if (!autoBitplanes && CheckBitplanes(bitplanes, err) != 0)
        return -1;
    if (ReadIndexedBmp(inputBmpPath, &img, err) != 0)
        return -1;
    n = (uint32_t)img.w * (uint32_t)img.h;

    if (autoBitplanes) {
        int32_t highest = 0;
        char line[600];
        for (uint32_t i = 0; i < n; i++)
            if (img.px[i] > highest)
                highest = img.px[i];
        bitplanes = highest <= 15 ? 4 : 8;
        snprintf(line, sizeof line, "  %s: max colour index %d -> %d bitplanes", Sg_PathFileName(outputRawPath),
                 (int)highest, (int)bitplanes);
        Log(t, line);
    }

    /* The C# encodes width / 8 bytes a plane row: the pixels of a last partial
     * byte are dropped, and only the pixels it encodes are checked against the
     * depth. Keep the picture to that width so Sg_AmigaRawEncode sees the same. */
    if ((img.w & 7) != 0) {
        int32_t w8 = img.w & ~7;
        if (w8 == 0) {
            /* The C# would write an empty RAW (and fail to pack it). */
            snprintf(err, 256, "BMP is %d pixels wide: too narrow for a planar RAW.", (int)img.w);
            SgImage_Free(&img);
            return -1;
        }
        for (int32_t y = 0; y < img.h; y++)
            memmove(img.px + (size_t)y * w8, img.px + (size_t)y * img.w, (size_t)w8);
        img.w = w8;
        n = (uint32_t)img.w * (uint32_t)img.h;
    }

    /* The C#'s message for an index the depth cannot hold (its encoder meets
     * the pixels in reading order, so the first such pixel is this one). */
    maxIndex = (1 << bitplanes) - 1;
    for (uint32_t i = 0; i < n; i++) {
        if (img.px[i] > maxIndex) {
            snprintf(err, 256, "Pixel index %d exceeds maximum %d for %d bitplanes.", (int)img.px[i],
                     (int)maxIndex, (int)bitplanes);
            SgImage_Free(&img);
            return -1;
        }
    }

    rc = Sg_AmigaRawEncode(&img, bitplanes, &raw, err);
    SgImage_Free(&img);
    if (rc != 0)
        return -1;
    rc = RncWriteAllBytes(t, outputRawPath, raw.data, raw.size, err);
    Sg_BufFree(&raw);
    return rc;
}

int32_t Sg_AmigaConvertRawToBmp(const SgAmigaTools *t, const char *inputRawPath,
                                const uint32_t *palette, int32_t paletteCount,
                                const char *outputBmpPath,
                                const uint32_t *newPalette, int32_t newCount,
                                int32_t bitplanes, int32_t colorCount, char *err)
{
    SgBuf raw;
    SgImage img;
    int32_t width, height, rc;

    if (CheckBitplanes(bitplanes, err) != 0)
        return -1;
    if (RncReadAllBytes(inputRawPath, &raw, err) != 0)
        return -1;

    /* DetectRawDimensions: the length must be exactly one of the two sizes. */
    if (Sg_AmigaRawDimensions(raw.size, bitplanes, &width, &height) != 0) {
        snprintf(err, 256,
                 "Unknown raw format: %u bytes with %d bitplanes. Expected %d bytes for 320x256, or %d bytes for 352x272.",
                 (unsigned)raw.size, (int)bitplanes, (int)(320 / 8 * bitplanes * 256),
                 (int)(352 / 8 * bitplanes * 272));
        Sg_BufFree(&raw);
        return -1;
    }
    rc = Sg_AmigaRawDecode(&raw, width, height, bitplanes, &img, err);
    Sg_BufFree(&raw);
    if (rc != 0)
        return -1;

    rc = Write8bppBmpFromIndices(outputBmpPath, img.px, img.w, img.h, palette, paletteCount, newPalette,
                                 newCount, colorCount, err);
    SgImage_Free(&img);
    return rc;
}

/* ------------------------------------------------------------------------
 * Program.cs RunAmiga
 * ------------------------------------------------------------------------ */

void Sg_AmigaOptionsInit(SgAmigaOptions *o)
{
    memset(o, 0, sizeof *o);
    o->bitplanes = 4;      /* 4 planes, 16 colours */
    o->colorCount = 16;
    o->amigaDir = "Amiga";
}

static void Take(const uint32_t *src, int32_t count, uint32_t *out, int32_t *outCount)
{
    memcpy(out, src, (size_t)count * sizeof *out);
    *outCount = count;
}

int32_t Sg_AmigaGetPaletteRequired(const SgAmigaOptions *o, const char *fileName,
                                   uint32_t pal[256], int32_t *palCount,
                                   uint32_t newPal[256], int32_t *newCount, char *err)
{
    char name[512];
    const uint16_t *pal12 = NULL;
    const uint32_t *dosNew = NULL;
    int32_t take = o->colorCount < 256 ? o->colorCount : 256;

    *palCount = 0;
    *newCount = 0;
    if (o->paletteName)
        return Sg_ResolvePalette(o->paletteName, o->colorCount, pal, palCount, err);

    FileNameWithoutExtension(name, sizeof name, fileName);

    if (Sg_StartsWithNoCase(name, "LOADER")) {
        /* name.Substring(6).ToLower() against "00" and "1" */
        const char *loaderIndex = name + 6;
        if (strcmp(loaderIndex, "00") == 0)
            pal12 = Sg_AmigaPaletteTitleLogo;
        else if (strcmp(loaderIndex, "1") == 0)
            pal12 = Sg_AmigaPaletteTitleScreen;
        else
            pal12 = Sg_AmigaPaletteTitleText;
        dosNew = Sg_DosPaletteMenu;
    } else if (Sg_StartsWithNoCase(name, "MENU") || Sg_StartsWithNoCase(name, "SOCCER")) {
        pal12 = Sg_AmigaPaletteMenu;
        dosNew = Sg_DosPaletteMenu;
    } else {
        /* name.Contains("DISK", OrdinalIgnoreCase) */
        for (const char *p = name; *p && !pal12; p++)
            if (Sg_StartsWithNoCase(p, "DISK"))
                pal12 = Sg_AmigaPaletteMenu, dosNew = Sg_DosPaletteMenu;
        if (!pal12 && (Sg_StartsWithNoCase(name, "CJC") || Sg_StartsWithNoCase(name, "CHARSET")))
            pal12 = Sg_AmigaPaletteMenu, dosNew = Sg_DosPaletteGame;
    }

    if (!pal12) {
        /* ResolvePalette(null, ...) throws ArgumentNullException */
        snprintf(err, 256, "Value cannot be null. (Parameter 'paletteName')");
        return -1;
    }
    Sg_AmigaPaletteFromAmiga12(pal12, 16, pal);
    *palCount = 16;
    if (o->colorCount > 16)
        Take(dosNew, take, newPal, newCount);
    return 0;
}

int32_t Sg_AmigaGetPaletteOptional(const SgAmigaOptions *o, uint32_t pal[256], int32_t *palCount, char *err)
{
    *palCount = 0;
    if (!o->paletteName)
        return 0;
    return Sg_ResolvePalette(o->paletteName, o->colorCount, pal, palCount, err);
}

/* One file of the mode: the conversion the C#'s switch picks. */
static int32_t ConvertOne(const SgAmigaTools *t, const SgAmigaOptions *o, const char *inputPath,
                          const char *outputPath, char *err)
{
    uint32_t pal[256], newPal[256];
    int32_t palCount, newCount;

    switch (o->output) {
    case SG_AMIGA_KIND_BMP:
        if (o->input == SG_AMIGA_KIND_MAP) {
            if (Sg_AmigaGetPaletteOptional(o, pal, &palCount, err) != 0)
                return -1;
            return Sg_AmigaConvertPitchMapToBmp(t, inputPath, palCount ? pal : NULL, palCount, outputPath, NULL, 0,
                                                o->bitplanes, o->colorCount, err);
        }
        if (o->input == SG_AMIGA_KIND_RAW) {
            if (Sg_AmigaGetPaletteRequired(o, inputPath, pal, &palCount, newPal, &newCount, err) != 0)
                return -1;
            return Sg_AmigaConvertRawToBmp(t, inputPath, pal, palCount, outputPath, newCount ? newPal : NULL,
                                           newCount, o->bitplanes, o->colorCount, err);
        }
        snprintf(err, 256, "Invalid combination: -output=bmp requires -map or -raw as input.");
        return -1;

    case SG_AMIGA_KIND_TILED:
        if (o->input != SG_AMIGA_KIND_MAP) {
            snprintf(err, 256, "Invalid combination: -output=tmj requires -map as input.");
            return -1;
        }
        if (Sg_AmigaGetPaletteRequired(o, inputPath, pal, &palCount, newPal, &newCount, err) != 0)
            return -1;
        return Sg_AmigaConvertPitchMapToTiled(t, inputPath, pal, palCount, outputPath, newCount ? newPal : NULL,
                                              newCount, o->bitplanes, o->colorCount, err);

    case SG_AMIGA_KIND_MAP:
        if (o->input == SG_AMIGA_KIND_TILED)
            return Sg_AmigaConvertTiledToPitchMap(t, inputPath, outputPath, SG_AMIGA_PITCH_MAX_TILES,
                                                  o->bitplanes, err);
        if (o->input == SG_AMIGA_KIND_BMP)
            return Sg_AmigaConvertFullPitchBmpToMap(t, inputPath, outputPath, SG_AMIGA_PITCH_MAX_TILES,
                                                    o->bitplanes, err);
        snprintf(err, 256, "Invalid combination: -output=map requires -tmj or -bmp as input.");
        return -1;

    case SG_AMIGA_KIND_RAW:
        if (o->input != SG_AMIGA_KIND_BMP) {
            snprintf(err, 256, "Invalid combination: -output=raw requires -bmp as input.");
            return -1;
        }
        /* Asked for and unused, as in the C#: a name without a rule fails. */
        if (Sg_AmigaGetPaletteRequired(o, inputPath, pal, &palCount, newPal, &newCount, err) != 0)
            return -1;
        return Sg_AmigaConvertBmpToRaw(t, inputPath, outputPath, o->autoBitplanes ? 0 : o->bitplanes, err);

    default:
        snprintf(err, 256, "Unknown output kind.");
        return -1;
    }
}

/* ---- the batch walk: Directory.EnumerateFiles(dir, glob, AllDirectories) ---- */

typedef struct {
    char   **items;
    int32_t  count, cap;
} StrList;

static int32_t StrListPush(StrList *l, const char *s)
{
    char *copy;
    if (l->count == l->cap) {
        int32_t nc = l->cap ? l->cap * 2 : 32;
        char **n = realloc(l->items, (size_t)nc * sizeof *n);
        if (!n)
            return -1;
        l->items = n;
        l->cap = nc;
    }
    copy = malloc(strlen(s) + 1);
    if (!copy)
        return -1;
    strcpy(copy, s);
    l->items[l->count++] = copy;
    return 0;
}

static void StrListFree(StrList *l)
{
    for (int32_t i = 0; i < l->count; i++)
        free(l->items[i]);
    free(l->items);
    memset(l, 0, sizeof *l);
}

static int CompareNames(const void *a, const void *b)
{
    return (int)Sg_CompareNoCase(*(const char *const *)a, *(const char *const *)b);
}

/* The files and the subfolders of `dir`, names only, each sorted as NTFS
 * returns them (case-insensitive, by upper case). -1 when unreadable. */
static int32_t ListEntries(const char *dir, StrList *files, StrList *dirs)
{
#ifdef _WIN32
    char pattern[1100];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    Sg_PathJoin(pattern, sizeof pattern, dir, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
            continue;
        if (StrListPush((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? dirs : files, fd.cFileName) != 0) {
            FindClose(h);
            return -1;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        char path[2048];
        StrList *into;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        Sg_PathJoin(path, sizeof path, dir, e->d_name);
        into = Sg_IsDir(path) ? dirs : Sg_IsFile(path) ? files : NULL;
        if (into && StrListPush(into, e->d_name) != 0) {
            closedir(d);
            return -1;
        }
    }
    closedir(d);
#endif
    if (files->count > 1)
        qsort(files->items, (size_t)files->count, sizeof *files->items, CompareNames);
    if (dirs->count > 1)
        qsort(dirs->items, (size_t)dirs->count, sizeof *dirs->items, CompareNames);
    return 0;
}

/* Every file under `root` whose name ends in `ext` (any case), in .NET's order:
 * a folder's files, then its subfolders queued, breadth first. */
static int32_t EnumerateFiles(const char *root, const char *ext, StrList *out, char *err)
{
    StrList queue = { 0 };
    int32_t head = 0, rc = 0;

    if (!Sg_IsDir(root)) {
        snprintf(err, 256, "Could not find a part of the path '%.200s'.", root);
        return -1;
    }
    if (StrListPush(&queue, root) != 0) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    while (head < queue.count && rc == 0) {
        StrList files = { 0 }, dirs = { 0 };
        const char *dir = queue.items[head++];
        char path[2048];

        if (ListEntries(dir, &files, &dirs) != 0) {
            snprintf(err, 256, "Access to the path '%.200s' is denied.", dir);
            rc = -1;
        }
        for (int32_t i = 0; rc == 0 && i < files.count; i++) {
            if (!Sg_EndsWithNoCase(files.items[i], ext))
                continue;
            Sg_PathJoin(path, sizeof path, dir, files.items[i]);
            if (StrListPush(out, path) != 0)
                rc = -1;
        }
        for (int32_t i = 0; rc == 0 && i < dirs.count; i++) {
            Sg_PathJoin(path, sizeof path, dir, dirs.items[i]);
            if (StrListPush(&queue, path) != 0)
                rc = -1;
        }
        if (rc != 0 && !err[0])
            snprintf(err, 256, "out of memory");
        StrListFree(&files);
        StrListFree(&dirs);
    }
    StrListFree(&queue);
    return rc;
}

int32_t Sg_AmigaRun(const SgAmigaOptions *o, char *err)
{
    SgAmigaTools t;
    const char *outDir, *inputGlob, *outputExt;
    StrList found = { 0 };
    int32_t converted = 0, rc = 0;

    err[0] = 0;
    t.writeRnc = !o->noRnc;
    t.log = o->log;
    t.logUser = o->logUser;

    if (o->fileCount >= 2)
        return ConvertOne(&t, o, o->files[0], o->files[1], err);

    /* Batch (0 or 1 positional arguments) */
    outDir = o->fileCount == 1 && o->files[0] && o->files[0][strspn(o->files[0], " \t\r\n")] ? o->files[0] : ".";
    if (Sg_MakeDirs(outDir) != 0) {
        snprintf(err, 256, "Could not create the folder '%.200s'.", outDir);
        return -1;
    }

    switch (o->output) {
    case SG_AMIGA_KIND_BMP: outputExt = ".bmp"; break;
    case SG_AMIGA_KIND_TILED: outputExt = SG_TILED_EXTENSION; break;
    default:
        snprintf(err, 256, "Batch mode currently supports only -output=bmp or -output=tmj in Amiga mode.");
        return -1;
    }
    switch (o->input) {
    case SG_AMIGA_KIND_RAW:
        if (o->output != SG_AMIGA_KIND_BMP) {
            snprintf(err, 256, "Amiga batch: -raw supports only -output=bmp.");
            return -1;
        }
        inputGlob = "*.raw";
        break;
    case SG_AMIGA_KIND_MAP:
        inputGlob = "*.map";
        break;
    default:
        snprintf(err, 256, "Amiga batch mode supports only -raw or -map inputs.");
        return -1;
    }

    if (EnumerateFiles(o->amigaDir ? o->amigaDir : ".", inputGlob + 1, &found, err) != 0)
        return -1;

    for (int32_t i = 0; i < found.count; i++) {
        char baseName[512], name[600], outPath[1200];

        FileNameWithoutExtension(baseName, sizeof baseName, found.items[i]);
        snprintf(name, sizeof name, "%s%s", baseName, outputExt);
        Combine(outPath, sizeof outPath, outDir, name);
        if (ConvertOne(&t, o, found.items[i], outPath, err) != 0) {
            rc = -1;
            break;
        }
        converted++;
    }
    StrListFree(&found);

    if (rc == 0 && converted == 0 && o->warn) {
        char line[600];
        snprintf(line, sizeof line, "Warning: No files matched '%s' in '%.500s'.", inputGlob,
                 o->amigaDir ? o->amigaDir : ".");
        o->warn(o->warnUser, line);
    }
    return rc;
}
