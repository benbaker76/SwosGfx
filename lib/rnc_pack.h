/*
 * rnc_pack.h -- RNC ProPack packing (methods 1 and 2), for files the game must
 * read packed.
 *
 * The game reads each data file whole into its screen buffer (about 91 KB) before
 * unpacking it, so a converted file bigger than that has to be stored packed, as
 * the original disks stored it. Sg_RncUnpack (sg_util.h) reverses this.
 *
 * Ported from SwosGfx (github.com/benbaker76/SwosGfx, MIT): RncProcessor.cs, DoPack
 * and everything it calls -- itself a port of lab313ru's rnc_propack_source. The
 * port keeps the encoder's choices exactly, so its output is byte for byte what
 * SwosGfx writes.
 *
 * The format. An 18-byte big-endian header:
 *
 *   +0  'R' 'N' 'C' method     method 1 or 2
 *   +4  u32 unpacked size
 *   +8  u32 packed size         bytes after the header
 *   +12 u16 CRC-16 of the unpacked data   (poly 0xA001, reflected, init 0)
 *   +14 u16 CRC-16 of the packed data
 *   +16 u8  leeway              how far the packed data may trail the unpacked
 *                               output when unpacking in place
 *   +17 u8  chunk count
 *
 * then a bit stream beginning with two bits (locked = 0, encrypted = 0 here) and
 * one or more chunks. Both methods are LZ77 with literal runs interleaved with
 * the bit stream as whole bytes; a chunk covers at most 0x3000 input bytes.
 *
 *   Method 1: bits are taken 16 at a time, little-endian words, LSB first. Each
 *     chunk starts with three Huffman tables (literal-run lengths, match offsets,
 *     match lengths; 5 bits of count then 4 bits of depth per symbol) and a 16-bit
 *     count of (run, match) pairs. Dictionary up to 0x8000, matches up to 0x1000.
 *   Method 2: bits are taken 8 at a time, MSB first, with fixed codes for literals
 *     and match lengths, and the low byte of each match offset sent as a whole
 *     byte. Dictionary up to 0x1000, matches up to 0xFF+2.
 *
 * MSVC-friendly C99: fixed-width types, no VLAs, the work buffers on the heap and
 * sized from the input.
 */
#ifndef SWOSGFX_RNC_PACK_H
#define SWOSGFX_RNC_PACK_H

#include <stdint.h>
#include "sg_util.h"

/* What SwosGfx writes (AmigaRncHelper.RncPack, used for every Amiga RAW and MAP
 * it outputs, i.e. `-bmp -output=raw` and `-bmp -output=map` without -no-rnc):
 * method 2, the default dictionary (0xFFFF) clamped to 0x1000, at most 0xFF
 * matches. The team files of scripts/swos_team_file.py use the same clamps. */
#define SG_RNC_SWOSGFX_METHOD  2

/* Pack `in` with RNC ProPack `method` (1 or 2), dictionary and match limits at
 * their per-method maximum (method 1: dictionary 0x8000, max match 0x1000;
 * method 2: 0x1000 and 0xFF) -- the clamps SwosGfx and swos_team_file.py apply.
 * out->data is malloc'd (Sg_BufFree). 0 on success; on failure `err` (256 bytes)
 * says why: the input is 18 bytes or less, is already RNC-packed, or does not
 * pack smaller than itself (the encoder cannot write a packed file larger than
 * its input). */
int32_t Sg_RncPack(const SgBuf *in, int32_t method, SgBuf *out, char *err);

/* The same with an explicit dictionary size (clamped to the method's maximum as
 * above; 0 means the maximum). */
int32_t Sg_RncPackEx(const SgBuf *in, int32_t method, uint16_t dictSize, SgBuf *out, char *err);

/* Sg_RncPack at SwosGfx's settings: method 2. */
int32_t Sg_RncPackLikeSwosGfx(const SgBuf *in, SgBuf *out, char *err);

#endif
