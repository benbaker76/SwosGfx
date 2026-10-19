/*
 * amiga_palette.h -- SwosGfx's palettes: the Amiga game's 12-bit tables, the
 * colour conversions, choosing a palette by name, and the -palettes export.
 *
 * A port of SwosGfx's AmigaPalette.cs plus Program.cs's ResolvePalette. The PC
 * game's 256-colour tables it draws on are in dos_palette.h; the paint-program
 * file writers in pal_file.h.
 *
 * Colours are 0xAARRGGBB (alpha 0xFF) or Amiga 12-bit 0x0RGB. A 12-bit colour
 * widens by nibble << 4 (0xF -> 0xF0, not 0xFF), as SwosGfx does, so the results
 * match its BMPs and the DOS tables.
 */
#ifndef SWOSGFX_AMIGA_PALETTE_H
#define SWOSGFX_AMIGA_PALETTE_H

#include <stdint.h>

#include "pal_file.h"

/* -pal-color= */
typedef enum {
    SG_PAL_COLOR_AMIGA12,
    SG_PAL_COLOR_RGB32,
    SG_PAL_COLOR_COUNT
} SgPalColor;

/* -pal-file= */
typedef enum {
    SG_PAL_FILE_ASM,
    SG_PAL_FILE_C,
    SG_PAL_FILE_PALETTE,
    SG_PAL_FILE_COUNT
} SgPalFile;

/* The option names, in enum order: "amiga12", "rgb32" / "asm", "c", "palette".
 * (The -pal-format= names are Sg_PalFormatNames in pal_file.h.) */
extern const char *const Sg_PalColorNames[SG_PAL_COLOR_COUNT];
extern const char *const Sg_PalFileNames[SG_PAL_FILE_COUNT];

/* ---- the tables (12-bit 0x0RGB, 16 entries) ------------------------------ */

#define SG_AMIGA_PITCH_PALETTES 7

/* Frozen, Muddy, Wet, Soft, Normal, Dry, Hard: the order of the pitch tables
 * here and in dos_palette.h. */
extern const char *const Sg_AmigaPitchPaletteNames[SG_AMIGA_PITCH_PALETTES];

extern const uint16_t Sg_AmigaPaletteMenu[16];
extern const uint16_t Sg_AmigaPaletteGame[16];
extern const uint16_t Sg_AmigaPaletteService[16];
extern const uint16_t Sg_AmigaPaletteTitleLogo[16];
extern const uint16_t Sg_AmigaPaletteTitleText[16];
extern const uint16_t Sg_AmigaPaletteTitleScreen[16];
extern const uint16_t Sg_AmigaPalettePanic[16];
extern const uint16_t Sg_AmigaPalettePitches[SG_AMIGA_PITCH_PALETTES][16];

/* ---- conversions --------------------------------------------------------- */

/* ToAmiga12: keep each channel's upper nibble. FromAmiga12: nibble << 4, alpha 0xFF. */
uint16_t Sg_AmigaToAmiga12(uint32_t argb);
uint32_t Sg_AmigaFromAmiga12(uint16_t amiga);
void     Sg_AmigaPaletteToAmiga12(const uint32_t *argb, int32_t count, uint16_t *out);
void     Sg_AmigaPaletteFromAmiga12(const uint16_t *amiga, int32_t count, uint32_t *out);

/* 1 << bitplanes; -1 outside 1..8 (the C# throws). */
int32_t  Sg_AmigaBitplanesToColors(int32_t bitplanes);

/* GetNearestColor: the entry of `pal` nearest `argb` by Euclidean RGB distance,
 * the first of equals. Returns the distance; *nearestIndex is -1 for an empty
 * palette. */
double   Sg_AmigaGetNearestColor(uint32_t argb, const uint32_t *pal, int32_t count,
                                 int32_t *nearestIndex);

/* QuantizeBmp(newPalette): remap `indices` (w*h bytes) drawn with `pal` onto
 * `newPal`. An entry whose colour equals newPal's at the same index keeps its
 * index, any other goes to the nearest new colour; index 0 (transparent) is
 * never touched. -1 with `err` for an index outside `pal`. The caller then uses
 * newPal as the image's palette, as the C# does. */
int32_t  Sg_AmigaQuantizeToPalette(uint8_t *indices, int32_t w, int32_t h,
                                   const uint32_t *pal, int32_t palCount,
                                   const uint32_t *newPal, int32_t newCount, char *err);

/* QuantizeBmp(colorCount): keep the first `colorCount` entries of `pal` and remap
 * every index at or above it to the nearest of those. Index 0 is never touched.
 * -1 with `err` for colorCount outside 1..palCount or an index outside `pal`. */
int32_t  Sg_AmigaQuantizeToCount(uint8_t *indices, int32_t w, int32_t h,
                                 const uint32_t *pal, int32_t palCount,
                                 int32_t colorCount, char *err);

/* ---- palettes by name (Program.cs ResolvePalette) ------------------------ */

#define SG_PALETTE_NAME_COUNT 13

/* Every name Sg_ResolvePalette takes, for a help text: the seven pitches, then
 * Menu, Game, Service, TitleLogo, TitleText, TitleScreen. */
extern const char *const Sg_PaletteNames[SG_PALETTE_NAME_COUNT];

/* The palette called `name` (any case, surrounding white space ignored) for an
 * image of `colorCount` colours, ARGB, into `out` (room for 256); *outCount is
 * its length. Above 16 colours a pitch, Menu or Game is the PC game's 256-colour
 * palette (a pitch through Sg_DosGetPitchPalette) and Service the PC's 16; the
 * title palettes are always the Amiga's 16. At 16 or fewer, all are the Amiga's
 * 16. The whole palette is returned, not `colorCount` of it -- the callers cut
 * it, as SwosGfx's do. An unknown name is -1 with SwosGfx's message in `err`
 * (which, like SwosGfx's, lists only the pitch names). */
int32_t  Sg_ResolvePalette(const char *name, int32_t colorCount, uint32_t out[256],
                           int32_t *outCount, char *err);

/* ---- output -------------------------------------------------------------- */

/* PaletteToPalette: write `argb` as <outDir>/<name><extension of format>
 * (outDir NULL or "" for the current folder). 0 on success. */
int32_t  Sg_AmigaPaletteToPalette(const char *outDir, const char *name,
                                  const uint32_t *argb, int32_t count,
                                  SgPalFormat format, char *err);

/* OutputAllPalettes, the -palettes mode: Menu, Game, Service and the seven
 * pitches, ten files in `outDir` (created; NULL or "" for the current folder),
 * named and laid out exactly as SwosGfx's. `count` is 16, 128 or 256; `full`
 * writes a pitch's whole palette rather than its nine entries; `format` is used
 * only with SG_PAL_FILE_PALETTE and SG_PAL_COLOR_RGB32. 0 on success; on failure
 * `err` (256 bytes) says why. */
int32_t  Sg_PalettesExport(const char *outDir, SgPalColor color, SgPalFile file,
                           int32_t count, int32_t full, SgPalFormat format, char *err);

#endif
