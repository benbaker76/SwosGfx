/*
 * jag2amiga.h -- the Amiga game's gameplay sprite banks from the Atari Jaguar
 * cartridge, Sensible Soccer International Edition (1995): the cartridge's
 * sheets unpacked and laid into the Amiga bank layout, the PC game's sprites
 * filling whatever the Jaguar has not got (-jag2amiga).
 *
 * The cartridge. The game's art is in LZ-packed blobs, each unpacking to one
 * 320x256 8bpp sheet, at cartridge addresses (mapped at $800000):
 *     CJCGRAFS $8947C0  CJCBITS  $8993D8  CJCBENCH $89D0B0  CJCTEAMG $8A02E8
 *     CJCTEAM1 $8AC828  CJCTEAM2 $8AFB58  CJCTEAM3 $8B2FE0
 * The packing ("lz-bits", jagfx compression.py) is LZSS read MSB first from a
 * bit stream, with an 8 KB ring whose write position starts at 1:
 *     1 <8 bits>              a literal byte
 *     0 <13 bits> <4 bits>    copy len+3 bytes from ring[offset] -- an ABSOLUTE
 *                             ring position, not a distance back
 *     0 <13 zero bits>        end of stream
 * The in-game palette is 256 16-bit CRY words at file offset $6FA42: chroma in
 * the high byte picks an R/G/B from Atari's tables, intensity in the low byte
 * scales it (c * i / 255).
 *
 * The banks. The Jaguar port kept the Amiga's bank coordinates, so the Amiga's
 * own image-command scripts (dos2amiga.h) say where every sprite is. For each
 * placement the window lifted from the sheet is the PC sprite's size (clipped
 * to the script's window); a placement the sheet leaves empty, or which the
 * Jaguar used for something else (the bank table in jag2amiga.c), gets the PC
 * sprite instead, its colours matched to the nearest Jaguar ones. Before that
 * walk the sheets are patched:
 *   - CJCTEAMn: the goal celebration, which the Jaguar game has not got, from
 *     the project's own drawings <setupDir>/CJCTEAMn_CELEB.bmp (48x32, the bank
 *     block at (144,64), Jaguar palette). A missing file leaves the cells to
 *     the PC sprites, as SwosGfx does.
 *   - CJCBENCH: the white-shirted manager (sprites 1189-1194; the Jaguar drew
 *     only the brown one), made here from the PC sprites with the shadow colour
 *     (8) on the Jaguar's shadow index 30, as two 48x24 crops at (16,24) and
 *     (256,24); and the SELECT SUBSTITUTE arrow (1170, 9x8 at (272,120)), the
 *     Jaguar's own arrow mirrored to point right with its drop shadow cast again
 *     one pixel down-right in the Jaguar's shades. SwosGfx read these three as
 *     CJCBENCH_PATCH_<x>_<y>.bmp; they are derived art, so they are derived here.
 *   A crop REPLACES its rectangle, zeros included, and the cells it covers are
 *   taken from the sheet whatever the bank table says.
 *
 * -jag2amiga (Sg_Jag2AmigaRun) also reads hand-drawn art from the Jaguar folder,
 * for every bank, as SwosGfx does: <bank>_CELEB.bmp (any bank, 48x32 at
 * (144,64)), <bank>_PATCH.bmp (a whole 320x256 bank, colour 0 transparent) and
 * <bank>_PATCH_<x>_<y>.bmp (a crop pasted at (x,y), zeros included), the crops
 * in name order (any case). The three derived CJCBENCH crops take their place
 * in that order under SwosGfx's file names, so a CJCBENCH_PATCH_16_24.bmp in
 * the folder replaces the derived one -- with SwosGfx's own files there the
 * result is the same either way.
 */
#ifndef SWOSGFX_JAG2AMIGA_H
#define SWOSGFX_JAG2AMIGA_H

#include <stdint.h>

#include "sg_image.h"
#include "sg_log.h"
#include "sg_util.h"

#define SG_JAG_ROM_SIZE    2097152u     /* the 2 MB cartridge image */

/* The six banks the game loads, in the order of the files. */
typedef enum {
    SG_JAG_CJCTEAM1, SG_JAG_CJCTEAM2, SG_JAG_CJCTEAM3,
    SG_JAG_CJCTEAMG, SG_JAG_CJCBITS, SG_JAG_CJCBENCH,
    SG_JAG_BANK_COUNT
} SgJagBank;

extern const char *const Sg_JagBankNames[SG_JAG_BANK_COUNT];   /* "CJCTEAM1", ... */

/* The six banks as 8bpp pictures in the Jaguar palette (<BANK>.bmp of
 * -jag2amiga), plus the raw CJCGRAFS sheet (its CJCGRAFS_JAGSHEET.bmp). `rom`
 * is the cartridge image in memory, `dosDir` the PC game folder; `celebDir`
 * holds CJCTEAMn_CELEB.bmp (NULL or missing files: the PC celebration).
 * `celebrations`, if not NULL, gets a bit per kit bank (1 << SG_JAG_CJCTEAMn)
 * whose hand-drawn celebration was used. Free each image with SgImage_Free.
 * 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_JagBanks(const SgBuf *rom, const char *dosDir, const char *celebDir,
                    SgImage banks[SG_JAG_BANK_COUNT], SgImage *grafsSheet,
                    uint32_t *celebrations, char *err);

/* SwosGfx -dos -jag2amiga: every file it writes, into `outDir` (made if need
 * be), from the cartridge image at `romPath` and the PC game in `dosDir`, with
 * the hand-drawn patches in `jagDir` (NULL or missing: none):
 *   CJCGRAFS CJCTEAM1 CJCTEAM2 CJCTEAM3 CJCTEAMG CJCBITS CJCBENCH .bmp
 *            the Jaguar banks (the PC sprites where the Jaguar has none);
 *   SOCCER_S CHARSET MENUS MENUS2 .bmp   from the PC game alone, as -dos2amiga;
 *   JAG_GAME.act                         the Jaguar palette, 768 bytes;
 *   makeTemplate: <bank>_DOS.bmp, <bank>_TODO.bmp, <bank>_placements.csv;
 *   dumpSheets:   <bank>_JAGSHEET.bmp (the sheet as unpacked).
 * (The environment variables SWOSGFX_JAG_TEMPLATE / SWOSGFX_DUMP_JAG_SHEETS
 * switch those on too, as in SwosGfx.) The lines SwosGfx prints go to `log`
 * (may be NULL), word for word. 0 on success; on failure `err` says why. */
int32_t Sg_Jag2AmigaRun(const char *dosDir, const char *romPath, const char *jagDir, const char *outDir,
                        int32_t makeTemplate, int32_t dumpSheets, SgLogFn log, void *user, char *err);

/* SwosGfx's default cartridge: the first *.jag, else *.j64, *.rom, *.bin in
 * `jagDir`, by name in any case. 0 and the path in out, -1 when there is none
 * (SwosGfx: "No Jaguar cartridge found in '<dir>'. Put the ROM there, or pass
 * -jag-rom=<path>."). */
int32_t Sg_JagFindRom(const char *jagDir, char *out, uint32_t cap);

/* The pieces. Unpack the lz-bits stream at cartridge address (or file offset)
 * `address` into exactly `size` bytes (malloc'd into out). 0 on success, -1 when
 * the address is outside the image or the stream ends short. */
int32_t Sg_JagUnpack(const SgBuf *rom, uint32_t address, uint32_t size, SgBuf *out, char *err);
/* The in-game palette, 0x00RRGGBB. 0 on success. */
int32_t Sg_JagGamePalette(const SgBuf *rom, uint32_t pal[256], char *err);

#endif
