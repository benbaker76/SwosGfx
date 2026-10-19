/*
 * amiga_tools.h -- SwosGfx's Amiga modes: pitch MAP <-> full-pitch BMP and
 * Tiled map (+ tileset BMP), and planar RAW <-> BMP.
 *
 * A port of SwosGfx's AmigaTools.cs (every conversion it has), the RNC-aware
 * file helpers of AmigaRncHelper.cs, and Program.cs RunAmiga with its palette
 * choice (GetPaletteRequired / GetPaletteOptional) and batch walk. Every file
 * written is byte for byte what SwosGfx writes for the same inputs and options.
 *
 * The formats.
 *
 *   Pitch MAP: a 42x55 grid of big-endian u32 cells (9240 bytes), each the byte
 *   offset of its 16x16 tile in the tile stream that follows: tile n at
 *   n * 16 * bitplanes * 2. A tile is 16 rows of `bitplanes` planes of 2 bytes,
 *   MSB the leftmost pixel, plane 0 the colour's least significant bit. The
 *   depth is not stored: the caller says it (-bitplanes, 4 for the OCS pitches,
 *   8 for AGA). On read, offsets counted in the fixed OCS tile size of 128 bytes
 *   are accepted at any depth too (DetectPitchOffsetStride, below).
 *
 *   RAW: a headerless planar picture, interleaved by row (amiga_raw.h): 320x256
 *   or 352x272, the size found from the length and the depth.
 *
 *   Both are read packed or not (a file starting "RNC" is unpacked) and written
 *   RNC-packed (method 2, as SwosGfx) unless SgAmigaTools.writeRnc is 0
 *   (-no-rnc).
 *
 * Errors: every function returns 0 or -1 with `err` (256 bytes) saying why, the
 * C#'s exception message where it had one. A malformed file is an error, never
 * a crash. Where SwosGfx wrote a file before it failed (map->tmj writes the
 * tileset BMP before it checks the map's cells; a batch stops at the first
 * failure with the files before it written), so does this.
 */
#ifndef SWOSGFX_AMIGA_TOOLS_H
#define SWOSGFX_AMIGA_TOOLS_H

#include <stdint.h>
#include "sg_log.h"

#define SG_AMIGA_PITCH_TILES_X     42
#define SG_AMIGA_PITCH_TILES_Y     55
#define SG_AMIGA_PITCH_TILE        16
#define SG_AMIGA_PITCH_MAX_TILES   284   /* what Program.cs passes as maxTiles */

/* The C# AmigaTools object plus AmigaRncHelper.DefaultWriteAsRnc. */
typedef struct {
    int32_t  writeRnc;   /* 1: MAP/RAW outputs RNC-packed (SwosGfx's default); 0: -no-rnc */
    SgLogFn  log;        /* Console.WriteLine lines (may be NULL) */
    void    *logUser;
} SgAmigaTools;

/* ---- the conversions (AmigaTools.cs), one per C# method -------------------
 *
 * Palettes are 0xAARRGGBB, `count` entries. Writing a BMP goes through the C#'s
 * Write8bppBmpFromIndices: with a newPalette the indices are moved onto it
 * (Sg_AmigaQuantizeToPalette) and it becomes the BMP's palette; without one the
 * palette is cut to its first colorCount entries and the indices above are moved
 * to the nearest of those (Sg_AmigaQuantizeToCount). So colorCount must not
 * exceed the palette's length, as in the C#. */

/* ConvertPitchMapToBmp: the 672x880 pitch. overridePalette NULL: the PC game
 * palette (256) above 4 bitplanes, else the Amiga game palette (16). */
int32_t Sg_AmigaConvertPitchMapToBmp(const SgAmigaTools *t, const char *mapFilePath,
                                     const uint32_t *overridePalette, int32_t overrideCount,
                                     const char *outputBmpPath,
                                     const uint32_t *newPalette, int32_t newCount,
                                     int32_t bitplanes, int32_t colorCount, char *err);

/* ConvertPitchMapToTiled: the Tiled map at dstMapPath, and its tileset (16 tiles
 * a row) as <dstMapPath without extension>.bmp beside it. */
int32_t Sg_AmigaConvertPitchMapToTiled(const SgAmigaTools *t, const char *mapFilePath,
                                       const uint32_t *palette, int32_t paletteCount,
                                       const char *dstMapPath,
                                       const uint32_t *newPalette, int32_t newCount,
                                       int32_t bitplanes, int32_t colorCount, char *err);

/* ConvertTiledToPitchMap: a 42x55 map of 16x16 tiles (JSON .tmj or XML .tmx)
 * and its tileset BMP back to a MAP. Tiles are de-duplicated in reading order;
 * more than maxTiles (> 0) is a warning on `log`, not an error. */
int32_t Sg_AmigaConvertTiledToPitchMap(const SgAmigaTools *t, const char *tiledMapPath,
                                       const char *outputMapPath, int32_t maxTiles,
                                       int32_t bitplanes, char *err);

/* ConvertFullPitchBmpToMap: an indexed BMP of at least 672x880 (its top-left
 * 672x880 is used; the palette is ignored) to a MAP. */
int32_t Sg_AmigaConvertFullPitchBmpToMap(const SgAmigaTools *t, const char *fullPitchBmpPath,
                                         const char *outputMapPath, int32_t maxTiles,
                                         int32_t bitplanes, char *err);

/* ConvertBmpToRaw: bitplanes 1..8, or 0 to pick 4 or 8 from the highest index
 * used (logging "  <out name>: max colour index N -> P bitplanes"). The C#'s
 * palette argument is unused there and is left out here. */
int32_t Sg_AmigaConvertBmpToRaw(const SgAmigaTools *t, const char *inputBmpPath,
                                const char *outputRawPath, int32_t bitplanes, char *err);

/* ConvertRawToBmp. */
int32_t Sg_AmigaConvertRawToBmp(const SgAmigaTools *t, const char *inputRawPath,
                                const uint32_t *palette, int32_t paletteCount,
                                const char *outputBmpPath,
                                const uint32_t *newPalette, int32_t newCount,
                                int32_t bitplanes, int32_t colorCount, char *err);

/* ---- the mode (Program.cs RunAmiga) -------------------------------------- */

typedef enum {
    SG_AMIGA_KIND_NONE,
    SG_AMIGA_KIND_MAP,      /* -map  / -output=map */
    SG_AMIGA_KIND_TILED,    /* -tmj (-tmx) / -output=tmj (tmx) */
    SG_AMIGA_KIND_BMP,      /* -bmp  / -output=bmp */
    SG_AMIGA_KIND_RAW       /* -raw  / -output=raw */
} SgAmigaKind;

typedef struct {
    SgAmigaKind  input, output;
    const char  *paletteName;     /* -palette=, NULL when not given */
    int32_t      bitplanes;       /* -bitplanes=N (default 4) */
    int32_t      autoBitplanes;   /* -bitplanes=auto (only -bmp -output=raw) */
    int32_t      colorCount;      /* opts.AmigaColorCount: 16, or 1 << N after
                                     -bitplanes=N, or -colors=N -- the last given */
    int32_t      noRnc;           /* -no-rnc */
    const char  *amigaDir;        /* batch input folder, already resolved (the C#
                                     combines -amiga-dir with the exe's folder) */
    const char *const *files;     /* positional arguments */
    int32_t      fileCount;       /* 2: in out; 0 or 1: batch into files[0] or "." */
    SgLogFn      log;             /* stdout lines (may be NULL) */
    void        *logUser;
    SgLogFn      warn;            /* stderr lines: the batch's "Warning: No files
                                     matched ..." (may be NULL) */
    void        *warnUser;
} SgAmigaOptions;

/* SwosGfx's defaults: bitplanes 4, colorCount 16, everything else 0/NULL. */
void    Sg_AmigaOptionsInit(SgAmigaOptions *o);

/* RunAmiga. With two files, one conversion:
 *   -map -output=bmp   palette: -palette or the default (GetPaletteOptional)
 *   -raw -output=bmp   palette: GetPaletteRequired(in)
 *   -map -output=tmj   palette: GetPaletteRequired(in)
 *   -tmj -output=map, -bmp -output=map   (maxTiles 284)
 *   -bmp -output=raw   GetPaletteRequired(in) is asked although unused, so
 *                      without -palette the input's name must have a rule,
 *                      as in SwosGfx
 * With 0 or 1 files, a batch: every *.raw (-raw -output=bmp) or *.map (-map
 * -output=bmp|tmj) under amigaDir, all folders down, any case, in the order
 * .NET's Directory.EnumerateFiles gives on Windows (a folder's files by name,
 * then its subfolders, breadth first), each to <outDir>/<name><.bmp|.tmj>. No
 * match is a warning, not an error. The first failure ends the batch. */
int32_t Sg_AmigaRun(const SgAmigaOptions *o, char *err);

/* GetPaletteRequired(fileName): with -palette, that palette (newCount 0).
 * Otherwise by the file's name, any case: LOADER00 the title logo, LOADER1 the
 * title screen, any other LOADER* the title text palette; MENU*, SOCCER* and
 * *DISK* the menu palette; CJC* and CHARSET* the menu palette too -- each with,
 * when colorCount > 16, a newPalette of the first colorCount entries of the PC
 * menu palette (the game palette for CJC/CHARSET). Any other name fails, as the
 * C# does ("Value cannot be null. (Parameter 'paletteName')"). newCount 0 means
 * no newPalette. */
int32_t Sg_AmigaGetPaletteRequired(const SgAmigaOptions *o, const char *fileName,
                                   uint32_t pal[256], int32_t *palCount,
                                   uint32_t newPal[256], int32_t *newCount, char *err);

/* GetPaletteOptional: -palette's palette, or *palCount 0 when none was given. */
int32_t Sg_AmigaGetPaletteOptional(const SgAmigaOptions *o, uint32_t pal[256],
                                   int32_t *palCount, char *err);

#endif
