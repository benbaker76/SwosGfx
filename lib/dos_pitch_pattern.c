/*
 * dos_pitch_pattern.c -- the PC game's pitch patterns (SwosGfx DosPitchPattern.cs).
 */
#include "dos_pitch_pattern.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The file `name` in `dir`, whatever its case; when there is none, the path the
 * C# would have tried (Path.Combine(dir, name)), for the message. */
static int32_t FindFile(const char *dir, const char *name, char *out, uint32_t cap)
{
    if (Sg_FindNoCase(dir, name, out, cap) == 0)
        return 0;
    Sg_PathJoin(out, cap, dir, name);
    return -1;
}

int32_t Sg_DosPitchPatternLoad(SgDosPitchPatternSet *set, const char *dir, char *err)
{
    memset(set, 0, sizeof *set);
    if (dir == NULL) {
        snprintf(err, 256, "Value cannot be null. (Parameter 'directoryPath')");
        return -1;
    }

    for (int32_t i = 0; i < SG_DOS_PITCH_MAX_PITCH_COUNT; i++) {
        char datName[32], blkName[32], datPath[1024], blkPath[1024];
        SgBuf datBytes, blkBytes;
        SgDosPitchPatterns *p = &set->pitches[i];

        snprintf(datName, sizeof datName, "pitch%d.dat", (int)(i + 1));
        snprintf(blkName, sizeof blkName, "pitch%d.blk", (int)(i + 1));
        int32_t haveDat = FindFile(dir, datName, datPath, sizeof datPath) == 0;
        int32_t haveBlk = FindFile(dir, blkName, blkPath, sizeof blkPath) == 0;

        /* Both checked before either is read, as the C# does. */
        if (!haveDat) {
            snprintf(err, 256, "Missing pattern index file: %.200s", datPath);
            return -1;
        }
        if (!haveBlk) {
            snprintf(err, 256, "Missing pattern data file: %.200s", blkPath);
            return -1;
        }
        if (Sg_ReadFile(datPath, &datBytes, err) != 0)
            return -1;
        if (Sg_ReadFile(blkPath, &blkBytes, err) != 0) {
            Sg_BufFree(&datBytes);
            return -1;
        }

        if (datBytes.size % 4 != 0) {
            snprintf(err, 256, "%.200s length is not a multiple of 4.", datPath);
            Sg_BufFree(&datBytes);
            Sg_BufFree(&blkBytes);
            return -1;
        }
        if (blkBytes.size % SG_DOS_PITCH_BYTES_PER_TILE != 0) {
            snprintf(err, 256, "%.200s length is not a multiple of %d bytes (16x16 tiles).", blkPath,
                     SG_DOS_PITCH_BYTES_PER_TILE);
            Sg_BufFree(&datBytes);
            Sg_BufFree(&blkBytes);
            return -1;
        }

        int32_t numPtrs = (int32_t)(datBytes.size / 4);
        p->patternIndices = (int32_t *)malloc((size_t)(numPtrs ? numPtrs : 1) * sizeof(int32_t));
        if (!p->patternIndices) {
            snprintf(err, 256, "out of memory");
            Sg_BufFree(&datBytes);
            Sg_BufFree(&blkBytes);
            return -1;
        }
        p->numPointers = numPtrs;

        /* The original C added the bank's address to each offset after loading
         * and later took (ptr - data) / 256; the index is all it ever needed. */
        for (int32_t j = 0; j < numPtrs; j++) {
            uint32_t offset = Sg_Le32(datBytes.data + (uint32_t)j * 4);
            if (offset % SG_DOS_PITCH_BYTES_PER_TILE != 0) {
                /* This *should* never happen for valid SWOS data. */
                snprintf(err, 256, "%.180s: pointer[%d] = %u is not aligned to %d bytes.", datPath, (int)j,
                         (unsigned)offset, SG_DOS_PITCH_BYTES_PER_TILE);
                Sg_BufFree(&datBytes);
                Sg_BufFree(&blkBytes);
                return -1;
            }
            p->patternIndices[j] = (int32_t)(offset / SG_DOS_PITCH_BYTES_PER_TILE);
        }
        Sg_BufFree(&datBytes);

        p->patternData = blkBytes.data;     /* ownership moves here */
        p->maxPatterns = (int32_t)(blkBytes.size / SG_DOS_PITCH_BYTES_PER_TILE);
        set->pitchCount = i + 1;
    }
    return 0;
}

void Sg_DosPitchPatternFree(SgDosPitchPatternSet *set)
{
    for (int32_t i = 0; i < SG_DOS_PITCH_MAX_PITCH_COUNT; i++) {
        free(set->pitches[i].patternIndices);
        free(set->pitches[i].patternData);
    }
    memset(set, 0, sizeof *set);
}

int32_t Sg_DosPitchPatternIndex(const SgDosPitchPatternSet *set, int32_t pitchIndex,
                                int32_t pointerIndex, int32_t *patternIndex, char *err)
{
    if (pitchIndex < 0 || pitchIndex >= set->pitchCount) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pitchIndex')");
        return -1;
    }
    const SgDosPitchPatterns *p = &set->pitches[pitchIndex];
    if (pointerIndex < 0 || pointerIndex >= p->numPointers) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pointerIndex')");
        return -1;
    }
    *patternIndex = p->patternIndices[pointerIndex];
    return 0;
}

const uint8_t *Sg_DosPitchPatternBytes(const SgDosPitchPatternSet *set, int32_t pitchIndex,
                                       int32_t patternIndex, char *err)
{
    if (pitchIndex < 0 || pitchIndex >= set->pitchCount) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'pitchIndex')");
        return NULL;
    }
    const SgDosPitchPatterns *p = &set->pitches[pitchIndex];
    if (patternIndex < 0 || patternIndex >= p->maxPatterns) {
        snprintf(err, 256, "Specified argument was out of the range of valid values. (Parameter 'patternIndex')");
        return NULL;
    }
    return p->patternData + (size_t)patternIndex * SG_DOS_PITCH_BYTES_PER_TILE;
}
