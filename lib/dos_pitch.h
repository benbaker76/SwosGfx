/*
 * dos_pitch.h -- the PC game's pitches (PITCHn.DAT + PITCHn.BLK) as the AGA
 * Amiga's pitch maps (SWCPICHn.MAP, P2020_n.MAP).
 *
 * A C port of scripts/make_pitch_aga.py; see dos_pitch.c for the formats.
 */
#ifndef SWOSGFX_DOS_PITCH_H
#define SWOSGFX_DOS_PITCH_H

#include <stdint.h>
#include "sg_util.h"

#define SG_PITCH_CELLS       (42 * 55)      /* 16x16 cells in a pitch */
#define SG_PITCH_TILE_BYTES  256            /* one 16x16 tile, 8bpp or 8 planes */
#define SG_PITCH_AGA_BUFFER  (544 * 1024)   /* PITCHFILE_MAX_SIZE on the AGA Amiga
                                               (include/display.h): a map must fit */

/* Convert one PC pitch already in memory. dat is 2310 little-endian u32 tile
 * offsets, blk the tile bank. outMap gets the AGA map, unpacked. 0 on success;
 * on failure `err` (256 bytes) says why. */
int32_t Sg_PitchPcToAga(const SgBuf *dat, const SgBuf *blk, SgBuf *outMap, char *err);

/* Read PITCHn.DAT and PITCHn.BLK (any case: the PC game's are upper case, SWOS
 * 2020's lower) from `dosDir` and convert them. pitch is 1..6. The result is
 * SWCPICHn.MAP for the PC game's folder, P2020_n.MAP for a SWOS 2020 grafs
 * folder; it is not RNC-packed (the game reads either). 0 on success. */
int32_t Sg_DosPitchToAgaMap(const char *dosDir, int32_t pitch, SgBuf *outMap, char *err);

#endif
