/*
 * dos_pitch_pattern.h -- the PC game's pitch patterns, all six pitches.
 *
 * A port of SwosGfx's DosPitchPattern.cs (the loading half: SwosGfx's save,
 * single-pattern BMP and bitmap-insert paths are not reached from its command
 * line and are not here). Each pitch is two files in the game folder:
 *
 *   PITCHn.DAT  little-endian u32s, one per pointer entry: a BYTE offset into
 *               the pattern bank, always a multiple of 256
 *   PITCHn.BLK  the pattern bank: 16x16 8bpp chunky tiles, 256 bytes each
 *
 * The pointer table is the 42-wide cell grid of the pitch, row by row; the
 * offsets are kept as pattern indices (offset / 256), as the C# does.
 */
#ifndef SWOSGFX_DOS_PITCH_PATTERN_H
#define SWOSGFX_DOS_PITCH_PATTERN_H

#include <stdint.h>

#define SG_DOS_PITCH_MAX_PITCH_COUNT  6     /* MaxPitchCount (MAX_PITCH) */
#define SG_DOS_PITCH_TILE_W           16
#define SG_DOS_PITCH_TILE_H           16
#define SG_DOS_PITCH_BYTES_PER_TILE   256

/* Patterns + pointer table for a single pitch (PitchPatterns). */
typedef struct {
    int32_t *patternIndices;    /* numPointers entries, each offset / 256 */
    int32_t  numPointers;
    uint8_t *patternData;       /* maxPatterns * 256 bytes */
    int32_t  maxPatterns;
} SgDosPitchPatterns;

typedef struct {
    SgDosPitchPatterns pitches[SG_DOS_PITCH_MAX_PITCH_COUNT];
    int32_t            pitchCount;
} SgDosPitchPatternSet;

/* LoadFromDirectory: pitch1.dat/.blk .. pitch6.dat/.blk from `dir` (names in
 * any case -- the GOG install has them upper case, and SwosGfx ran on Windows).
 * All six or nothing: the first missing or malformed file fails with SwosGfx's
 * message in `err` ("Missing pattern index file: DIR/pitch3.dat", "... length
 * is not a multiple of 4.", "... pointer[j] = N is not aligned to 256 bytes.").
 * A pointer past the bank is NOT an error here, as in the C#: it fails when it
 * is drawn (Sg_DosPitchPatternBytes). 0 on success; free with
 * Sg_DosPitchPatternFree either way. */
int32_t Sg_DosPitchPatternLoad(SgDosPitchPatternSet *set, const char *dir, char *err);
void    Sg_DosPitchPatternFree(SgDosPitchPatternSet *set);

/* IDosPitchPatternSource.GetPatternIndexForPointer / GetPatternBytes: -1 (NULL)
 * with `err` when the index is out of range, where the C# throws. */
int32_t        Sg_DosPitchPatternIndex(const SgDosPitchPatternSet *set, int32_t pitchIndex,
                                       int32_t pointerIndex, int32_t *patternIndex, char *err);
const uint8_t *Sg_DosPitchPatternBytes(const SgDosPitchPatternSet *set, int32_t pitchIndex,
                                       int32_t patternIndex, char *err);

#endif
