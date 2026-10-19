/*
 * amiga_raw.h -- Amiga planar RAW pictures: decode, encode, and the 256-colour
 * conversion of the Amiga-only full-screen pictures for the AGA build.
 *
 * A RAW is a headerless planar picture, INTERLEAVED by row: for each row, plane 0's
 * bytes (w/8 of them), then plane 1's, ... then plane N-1's. In each byte the most
 * significant bit is the leftmost pixel, and plane 0 is the least significant bit
 * of the colour index. The original disks keep them RNC-packed; the game reads a
 * packed or unpacked file alike. Sizes in use: 320x256 (the banks) and 352x272
 * (the full-screen pictures), at 4 planes (OCS) or 8 (AGA).
 *
 * Ported from SwosGfx (github.com/benbaker76/SwosGfx): AmigaTools.cs
 * ConvertRawToBmp / ConvertBmpToRaw / DetectRawDimensions, the palette choice of
 * Program.cs RunAmiga GetPaletteRequired, and AmigaPalette.cs QuantizeBmp.
 */
#ifndef SWOSGFX_AMIGA_RAW_H
#define SWOSGFX_AMIGA_RAW_H

#include <stdint.h>
#include "sg_util.h"
#include "sg_image.h"

/* Decode an UNPACKED planar RAW of w x h with `planes` (1-8) into indices (the
 * palette is left black). The buffer must hold at least w/8*planes*h bytes; w must
 * be a multiple of 8. 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_AmigaRawDecode(const SgBuf *unpackedRaw, int32_t w, int32_t h, int32_t planes,
                          SgImage *out, char *err);

/* Encode indices as an unpacked planar RAW. planes 1-8, or 0 for SwosGfx's
 * -bitplanes=auto: 4 if no index above 15 is used, else 8. An index that does not
 * fit the depth is an error. out->data is malloc'd (Sg_BufFree). */
int32_t Sg_AmigaRawEncode(const SgImage *img, int32_t planes, SgBuf *out, char *err);

/* The size a planes-deep RAW of `bytes` bytes has: 320x256 or 352x272. 0 when one
 * matches, -1 when none does. */
int32_t Sg_AmigaRawDimensions(uint32_t bytes, int32_t planes, int32_t *w, int32_t *h);

/* SwosGfx step [1/7] for one picture (`-raw -output=bmp -colors=256`): decode an
 * original 4-plane RAW (packed or not) named `name` (file name without extension,
 * any case, e.g. "LOADER1") with the Amiga palette the game shows it in, then remap
 * its colours onto the 256-colour DOS palette that the AGA build uses. The result
 * carries the DOS palette, as SwosGfx's BMP does.
 *   LOADER00 -> title logo palette, LOADER1 -> title screen, other LOADER* -> title
 *   text; MENU*, SOCCER*, *DISK* -> menu palette; all of those onto the DOS menu
 *   palette. CJC*, CHARSET* -> menu palette onto the DOS game palette.
 * Other names have no palette of their own in SwosGfx and are an error. */
int32_t Sg_AmigaPictureTo256(const char *name, const SgBuf *packedOrUnpackedRaw,
                             SgImage *out, char *err);

/* SwosGfx's palettes. The Amiga ones are 16 entries of 12-bit 0x0RGB (expand
 * with Sg_AmigaPal12To24: nibble << 4, as SwosGfx does); the DOS ones 0x00RRGGBB
 * (DosPalette.cs Menu / Game; named for this file so a DOS converter may keep its own). */
extern const uint16_t Sg_AmigaPal12Menu[16];
extern const uint16_t Sg_AmigaPal12TitleLogo[16];
extern const uint16_t Sg_AmigaPal12TitleText[16];
extern const uint16_t Sg_AmigaPal12TitleScreen[16];
extern const uint32_t Sg_AmigaRawDosMenuPal[256];
extern const uint32_t Sg_AmigaRawDosGamePal[256];

void    Sg_AmigaPal12To24(const uint16_t *pal12, int32_t count, uint32_t *out);

/* AmigaPalette.QuantizeBmp(newPalette): remap indices drawn with `oldPal`
 * (oldCount entries) onto `newPal` (newCount). An entry whose colour equals the new
 * palette's at the same index keeps its index; any other goes to the nearest new
 * colour by RGB distance, the first on a tie. Index 0 is never changed. */
void    Sg_AmigaRemapToPalette(SgImage *img, const uint32_t *oldPal, int32_t oldCount,
                               const uint32_t *newPal, int32_t newCount);

#endif
