/*
 * dos_pitch.c -- the PC game's pitches as the AGA Amiga's pitch maps.
 *
 * The AGA pitch map is OpenSWOS's: its AGA Amiga build draws the PC game's
 * pitches at 256 colours. A port of OpenSWOS's scripts/make_pitch_aga.py.
 *
 * The PC pitch is two files:
 *   PITCHn.DAT  2310 little-endian u32s, one per 16x16 cell of the 42 x 55
 *               grid, row by row: each is a BYTE offset into the tile bank
 *   PITCHn.BLK  the tile bank: 256-byte chunky tiles, 16 rows of 16 indices
 *
 * The AGA map (OpenSWOS src/pitch.c, FixupAndColorPitchFile) is the same two parts in
 * the Amiga's form, one after the other:
 *   +0          the 2310 offsets, big-endian, unchanged (a tile is 256 bytes
 *               in both forms, so the offsets still hold)
 *   +9240       each tile as 8 interleaved bitplanes, a row at a time: for
 *               each of 16 rows, plane 0's word, plane 1's ... plane 7's
 *               (16 bytes a row, 256 a tile). The word's top bit is the left
 *               pixel; plane 0 is the index's lowest bit.
 * The colours stay the PC palette's indices: that is the AGA build's palette.
 *
 * SWOS 2020's pitches (lower-case names) are the same PC format and become
 * P2020_n.MAP the same way. They are larger than the PC game's (more
 * distinct tiles), so a caller should check the size against
 * SG_PITCH_AGA_BUFFER; the game reads them unpacked straight into its buffer.
 */
#include "dos_pitch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t Sg_PitchPcToAga(const SgBuf *dat, const SgBuf *blk, SgBuf *outMap, char *err)
{
    uint32_t tiles, i, t, maxOff = 0;
    uint8_t *o;

    outMap->data = NULL;
    outMap->size = 0;
    if (dat->size != SG_PITCH_CELLS * 4 || blk->size % SG_PITCH_TILE_BYTES) {
        snprintf(err, 256, "not a PC pitch: DAT %u bytes, BLK %u bytes",
                 (unsigned)dat->size, (unsigned)blk->size);
        return -1;
    }
    for (i = 0; i < SG_PITCH_CELLS; i++) {
        uint32_t off = Sg_Le32(dat->data + i * 4);
        if (off > maxOff)
            maxOff = off;
    }
    if ((uint64_t)maxOff + SG_PITCH_TILE_BYTES > blk->size) {
        snprintf(err, 256, "the map points past the tile bank");
        return -1;
    }
    tiles = blk->size / SG_PITCH_TILE_BYTES;
    outMap->size = SG_PITCH_CELLS * 4 + tiles * SG_PITCH_TILE_BYTES;
    outMap->data = (uint8_t *)malloc(outMap->size ? outMap->size : 1);
    if (!outMap->data) {
        outMap->size = 0;
        snprintf(err, 256, "out of memory");
        return -1;
    }
    o = outMap->data;
    for (i = 0; i < SG_PITCH_CELLS; i++) {
        uint32_t off = Sg_Le32(dat->data + i * 4);
        *o++ = (uint8_t)(off >> 24);
        *o++ = (uint8_t)(off >> 16);
        *o++ = (uint8_t)(off >> 8);
        *o++ = (uint8_t)off;
    }
    for (t = 0; t < tiles; t++) {
        const uint8_t *tile = blk->data + t * SG_PITCH_TILE_BYTES;
        int32_t row, plane, x;
        for (row = 0; row < 16; row++) {
            const uint8_t *px = tile + row * 16;
            for (plane = 0; plane < 8; plane++) {
                uint32_t w = 0;
                for (x = 0; x < 16; x++)
                    w |= (uint32_t)((px[x] >> plane) & 1) << (15 - x);
                *o++ = (uint8_t)(w >> 8);
                *o++ = (uint8_t)w;
            }
        }
    }
    return 0;
}

int32_t Sg_DosPitchToAgaMap(const char *dosDir, int32_t pitch, SgBuf *outMap, char *err)
{
    char name[32], path[1024];
    SgBuf dat = { NULL, 0 }, blk = { NULL, 0 };
    int32_t rc;

    outMap->data = NULL;
    outMap->size = 0;
    if (pitch < 1 || pitch > 6) {
        snprintf(err, 256, "there is no pitch %d (1..6)", (int)pitch);
        return -1;
    }
    snprintf(name, sizeof name, "PITCH%d.DAT", (int)pitch);
    if (Sg_FindNoCase(dosDir, name, path, sizeof path) != 0) {
        snprintf(err, 256, "%s is not in %s", name, dosDir);
        return -1;
    }
    if (Sg_ReadFile(path, &dat, err) != 0)
        return -1;
    snprintf(name, sizeof name, "PITCH%d.BLK", (int)pitch);
    if (Sg_FindNoCase(dosDir, name, path, sizeof path) != 0) {
        snprintf(err, 256, "%s is not in %s", name, dosDir);
        Sg_BufFree(&dat);
        return -1;
    }
    if (Sg_ReadFile(path, &blk, err) != 0) {
        Sg_BufFree(&dat);
        return -1;
    }
    rc = Sg_PitchPcToAga(&dat, &blk, outMap, err);
    Sg_BufFree(&dat);
    Sg_BufFree(&blk);
    return rc;
}
