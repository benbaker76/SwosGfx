/*
 * rnc_pack.c -- RNC ProPack packing; see rnc_pack.h.
 *
 * A line-for-line port of SwosGfx's RncProcessor.cs pack path (DoPack,
 * DoPackData, CompressData1/2, Proc6, FindMatches, EncodeMatches, the Huffman
 * builders Proc16/17/18/19/20), which ports lab313ru's rnc_propack_source. The
 * names are kept so the two can be read side by side. Every ushort of the C# is
 * a uint16_t here, so the wraps are the same.
 */
#include "rnc_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RNC_HEADER_SIZE 0x12
#define MEM1_SIZE       0xFFFF          /* the window: dictionary + block */
#define HASH_SIZE       0x10000         /* indexed & 0x7FFF, as the C# */
#define TEMP_SIZE       (0xFFFF * 6 + 16) /* (run, length, offset) words of one chunk */

static const uint16_t kCrcTable[256] = {
    0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
    0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
    0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
    0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
    0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
    0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
    0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
    0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
    0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
    0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
    0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
    0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
    0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
    0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
    0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
    0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
    0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
    0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
    0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
    0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
    0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
    0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
    0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
    0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
    0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
    0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
    0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
    0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
    0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
    0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
    0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
    0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

static const uint8_t kMatchCountBits[7]       = { 0x00, 0x0E, 0x08, 0x0A, 0x12, 0x13, 0x16 };
static const uint8_t kMatchCountBitsCount[7]  = { 0, 4, 4, 4, 5, 5, 5 };
static const uint8_t kMatchOffsetBits[16]     = { 0x00, 0x06, 0x08, 0x09, 0x15, 0x17, 0x1D, 0x1F,
                                                  0x28, 0x29, 0x2C, 0x2D, 0x38, 0x39, 0x3C, 0x3D };
static const uint8_t kMatchOffsetBitsCount[16] = { 1, 3, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6 };

typedef struct {
    uint32_t l1;        /* frequency */
    uint16_t l2;        /* next in the merged list, 0xFFFF = end */
    uint32_t l3;        /* code */
    uint16_t bitDepth;
} Huftable;

typedef struct {
    uint16_t maxMatches, encKey, dictSize;
    int32_t  packBlockSize, method;
    int32_t  fileSize;

    int32_t  bytesLeft, packedSize, processedSize, v7, packBlockPos;
    uint16_t packToken, bitCount, v11, lastMinOffset;
    int32_t  v17, packBlockLeftSize;
    uint16_t matchCount, matchOffset;
    int32_t  v20, v21;

    int32_t  unpackedSize;
    uint16_t unpackedCrc, packedCrc;
    int32_t  leeway, chunksCount;

    uint8_t  *mem1;
    uint16_t *mem2, *mem3, *mem4, *mem5;

    const uint8_t *input;
    uint8_t  *output, *temp;
    int32_t  inputOffset, outputOffset, tempOffset;

    uint8_t  tmpCrcData[2048];
    Huftable rawTable[16], posTable[16], lenTable[16];

    int32_t  packBlockStartIndex, packBlockMaxIndex, packBlockEndIndex;

    int32_t  overflow;  /* the packed data reached the input's size: output cut */
} Vars;

/* --- buffer helpers ---------------------------------------------------- */

static uint16_t PeekWordBE(const uint8_t *buf, int32_t i)
{
    return (uint16_t)((buf[i] << 8) | buf[i + 1]);
}

static void WriteByte(uint8_t *buf, int32_t *off, uint8_t b) { buf[(*off)++] = b; }

static void WriteWordBE(uint8_t *buf, int32_t *off, uint16_t val)
{
    WriteByte(buf, off, (uint8_t)(val >> 8));
    WriteByte(buf, off, (uint8_t)val);
}

static uint16_t ReadWordBE(const uint8_t *buf, int32_t *off)
{
    uint16_t w = (uint16_t)((buf[*off] << 8) | buf[*off + 1]);
    *off += 2;
    return w;
}

static void WriteDwordBE(uint8_t *buf, int32_t *off, uint32_t val)
{
    WriteWordBE(buf, off, (uint16_t)(val >> 16));
    WriteWordBE(buf, off, (uint16_t)val);
}

static void RorW(uint16_t *x)
{
    *x = (uint16_t)((*x & 1) ? (0x8000 | (*x >> 1)) : (*x >> 1));
}

static void InitDicts(Vars *v)
{
    int32_t i;
    for (i = 0; i < 0x8000; ++i) {
        v->mem2[i] = v->dictSize;
        v->mem3[i] = v->dictSize;
    }
    for (i = 0; i < v->dictSize; ++i) {
        v->mem5[i & 0x7FFF] = 0;
        v->mem4[i & 0x7FFF] = (uint16_t)i;
    }
    v->lastMinOffset = 0;
}

static void UpdatePackedCrc(Vars *v, uint8_t b)
{
    v->packedCrc = (uint16_t)(kCrcTable[(v->packedCrc & 0xFF) ^ b] ^ (v->packedCrc >> 8));
    v->packedSize++;
}

static void UpdateUnpackedCrc(Vars *v, uint8_t b)
{
    v->unpackedCrc = (uint16_t)(kCrcTable[(v->unpackedCrc & 0xFF) ^ b] ^ (v->unpackedCrc >> 8));
    v->processedSize++;
}

static void WriteToOutput(Vars *v, uint8_t b)
{
    if (v->packedSize >= v->fileSize - RNC_HEADER_SIZE) {
        v->overflow = 1;        /* the C# drops it silently; here it fails the pack */
        return;
    }
    WriteByte(v->output, &v->outputOffset, b);
    UpdatePackedCrc(v, b);
}

static uint8_t ReadFromInput(Vars *v)
{
    uint8_t b = v->input[v->inputOffset++];
    UpdateUnpackedCrc(v, b);
    return b;
}

/* --- bit writers ------------------------------------------------------- */

static void FlushToken(Vars *v)
{
    int32_t i;
    for (i = 0; i < v->v11; ++i)
        WriteToOutput(v, v->tmpCrcData[i]);
    v->v11 = 0;
    if (v->processedSize > v->packedSize && v->processedSize - v->packedSize > v->leeway)
        v->leeway = v->processedSize - v->packedSize;
    v->bitCount = 0;
    v->packToken = 0;
}

static void WriteBitsM2(Vars *v, uint16_t value, int32_t count)
{
    uint32_t mask = (uint32_t)1 << (count - 1);
    while (count-- > 0) {
        v->packToken = (uint16_t)(v->packToken << 1);
        if (value & mask)
            v->packToken++;
        mask >>= 1;
        v->bitCount++;
        if (v->bitCount == 8) {
            WriteToOutput(v, (uint8_t)v->packToken);
            FlushToken(v);
        }
    }
}

static void WriteBitsM1(Vars *v, uint16_t value, int32_t count)
{
    while (count-- > 0) {
        v->packToken = (uint16_t)((v->packToken >> 1) | ((value & 1) ? 0x8000 : 0));
        value >>= 1;
        v->bitCount++;
        if (v->bitCount == 16) {
            WriteToOutput(v, (uint8_t)v->packToken);
            WriteToOutput(v, (uint8_t)(v->packToken >> 8));
            FlushToken(v);
        }
    }
}

static void WriteBits(Vars *v, uint16_t bits, int32_t count)
{
    if (v->method == 2)
        WriteBitsM2(v, bits, count);
    else
        WriteBitsM1(v, bits, count);
}

/* --- match finder ------------------------------------------------------ */

static void FindMatches(Vars *v)
{
    const uint8_t *m = v->mem1;
    int32_t start = v->packBlockStartIndex;
    int32_t span = v->packBlockEndIndex - start;
    int32_t matchOffset = 1;
    uint16_t offset;

    v->matchCount = 1;
    v->matchOffset = 0;

    while (matchOffset < span && m[start + matchOffset] == m[start])
        matchOffset++;

    offset = v->mem2[PeekWordBE(m, start) & 0x7FFF];

    for (;;) {
        uint16_t restore, minOffset;
        if (offset == v->dictSize) {
            if (v->matchCount == 2 && v->matchOffset > 0x100) {
                v->matchCount = 1;
                v->matchOffset = 0;
            }
            break;
        }

        restore = v->mem4[offset & 0x7FFF];
        minOffset = v->lastMinOffset;
        if (minOffset <= offset)
            minOffset = (uint16_t)(minOffset + v->dictSize);
        minOffset = (uint16_t)(minOffset - offset);

        if (PeekWordBE(m, start - minOffset) == PeekWordBE(m, start)) {
            uint16_t maxCount = v->mem5[offset & 0x7FFF];
            if (maxCount <= minOffset) {
                if (maxCount > matchOffset) {
                    minOffset = (uint16_t)(minOffset - maxCount + matchOffset);
                    maxCount = (uint16_t)matchOffset;
                }
                if (maxCount == matchOffset) {
                    while (maxCount < span && m[start + maxCount] == m[start + maxCount - minOffset])
                        maxCount++;
                }
            } else {
                minOffset = 1;
                maxCount = (uint16_t)matchOffset;
            }

            if (maxCount > v->maxMatches)
                maxCount = v->maxMatches;
            if (maxCount >= v->matchCount) {
                v->matchCount = maxCount;
                v->matchOffset = minOffset;
            }
        }
        offset = restore;
    }
}

/* A match is kept only if the one starting a byte later is not longer (lazy). */
static void FindAndCheckMatches(Vars *v)
{
    FindMatches(v);
    if (v->matchCount >= 2 && v->packBlockMaxIndex - v->packBlockStartIndex >= 3) {
        uint16_t count = v->matchCount;
        uint16_t offset = v->matchOffset;
        uint16_t minOffset = v->lastMinOffset;

        v->lastMinOffset = (uint16_t)((v->lastMinOffset + 1) % v->dictSize);
        v->packBlockStartIndex++;
        FindMatches(v);
        v->packBlockStartIndex--;
        v->lastMinOffset = minOffset;

        if (count < v->matchCount) {
            count = 1;
            offset = 0;
        }
        v->matchCount = count;
        v->matchOffset = offset;
    }
}

static int32_t BitsCount(int32_t value)
{
    int32_t count = 1;
    while ((value >>= 1) != 0)
        count++;
    return count;
}

static void UpdateBitsTable(Vars *v, Huftable *data, uint16_t bits)
{
    int32_t idx = bits <= 1 ? bits : BitsCount(bits);
    data[idx].l1++;
    WriteWordBE(v->temp, &v->tempOffset, bits);
}

/* Enter w bytes from packBlockStartIndex into the hash chains and step over them. */
static void EncodeMatches(Vars *v, uint16_t w)
{
    const uint8_t *m = v->mem1;
    for (;;) {
        uint16_t restore = v->mem4[v->lastMinOffset & 0x7FFF];
        uint16_t bw;
        int32_t count;

        v->mem4[v->lastMinOffset & 0x7FFF] = v->dictSize;
        if (restore != v->lastMinOffset) {
            uint16_t bufferWord = PeekWordBE(m, v->packBlockStartIndex - v->dictSize);
            v->mem2[bufferWord & 0x7FFF] = restore;
            if (v->dictSize == restore)
                v->mem3[bufferWord & 0x7FFF] = v->dictSize;
        }

        bw = PeekWordBE(m, v->packBlockStartIndex);
        if (v->mem2[bw & 0x7FFF] == v->dictSize)
            v->mem2[bw & 0x7FFF] = v->lastMinOffset;
        else
            v->mem4[v->mem3[bw & 0x7FFF] & 0x7FFF] = v->lastMinOffset;
        v->mem3[bw & 0x7FFF] = v->lastMinOffset;

        count = 1;
        while (count < v->packBlockEndIndex - v->packBlockStartIndex &&
               m[v->packBlockStartIndex + count] == m[v->packBlockStartIndex])
            count++;
        v->mem5[v->lastMinOffset & 0x7FFF] = (uint16_t)count;

        for (;;) {
            v->lastMinOffset = (uint16_t)((v->lastMinOffset + 1) % v->dictSize);
            v->packBlockStartIndex++;
            if (--w == 0)
                return;
            if (--count <= 1)
                break;
            v->mem5[v->lastMinOffset & 0x7FFF] = (uint16_t)count;
            if (v->lastMinOffset != v->mem4[v->lastMinOffset & 0x7FFF]) {
                restore = v->mem4[v->lastMinOffset & 0x7FFF];
                v->mem4[v->lastMinOffset & 0x7FFF] = v->lastMinOffset;
                bw = PeekWordBE(m, v->packBlockStartIndex - v->dictSize);
                v->mem2[bw & 0x7FFF] = restore;
                if (v->dictSize == restore)
                    v->mem3[bw & 0x7FFF] = v->dictSize;
            }
        }
    }
}

/* Parse one chunk into temp: run, (length-2, offset-1), run, ..., run. */
static void Proc6(Vars *v)
{
    uint32_t dataLength = 0;

    v->v17 = 0;
    v->packBlockLeftSize = v->packBlockSize;
    v->inputOffset = v->v7 + v->packBlockPos;
    v->tempOffset = 0;

    while (v->bytesLeft != 0 || v->packBlockPos != 0) {
        int32_t sizeToRead = 0xFFFF - v->dictSize - v->packBlockPos;
        if (v->bytesLeft < sizeToRead)
            sizeToRead = v->bytesLeft;

        v->packBlockStartIndex = v->dictSize;
        memcpy(v->mem1 + v->packBlockStartIndex + v->packBlockPos, v->input + v->inputOffset,
               (size_t)sizeToRead);
        v->inputOffset += sizeToRead;

        v->bytesLeft -= sizeToRead;
        v->packBlockPos += sizeToRead;

        v->packBlockMaxIndex = v->packBlockStartIndex + v->packBlockPos;
        v->packBlockEndIndex = v->packBlockStartIndex + v->packBlockPos;
        if (v->packBlockLeftSize < v->packBlockPos)
            v->packBlockMaxIndex = v->packBlockStartIndex + v->packBlockLeftSize;

        while (v->packBlockStartIndex < v->packBlockMaxIndex - 1 && v->v17 < 0xFFFE) {
            FindAndCheckMatches(v);
            if (v->matchCount >= 2) {
                if (v->packBlockStartIndex + v->matchCount <= v->packBlockMaxIndex) {
                    UpdateBitsTable(v, v->rawTable, (uint16_t)dataLength);
                    UpdateBitsTable(v, v->posTable, (uint16_t)(v->matchCount - 2));
                    UpdateBitsTable(v, v->lenTable, (uint16_t)(v->matchOffset - 1));
                    EncodeMatches(v, v->matchCount);
                    v->v17++;
                    dataLength = 0;
                } else {
                    if (v->v17 != 0)
                        break;
                    v->matchCount = (uint16_t)(v->packBlockMaxIndex - v->packBlockStartIndex);
                }
            } else {
                EncodeMatches(v, 1);
                dataLength++;
            }
        }

        v->packBlockPos = v->packBlockEndIndex - v->packBlockStartIndex;
        memmove(v->mem1, v->mem1 + v->packBlockStartIndex - v->dictSize,
                (size_t)(v->dictSize + v->packBlockPos));

        if (v->packBlockMaxIndex < v->packBlockEndIndex ||
            (v->packBlockMaxIndex == v->packBlockEndIndex && v->bytesLeft == 0) ||
            v->v17 == 0xFFFE)
            break;

        v->packBlockLeftSize -= v->packBlockStartIndex - v->dictSize;
    }

    if (v->packBlockMaxIndex == v->packBlockEndIndex && v->bytesLeft == 0 && v->v17 != 0xFFFE)
        dataLength += (uint32_t)v->packBlockPos;

    UpdateBitsTable(v, v->rawTable, (uint16_t)dataLength);
    v->v17++;
    v->tempOffset = 0;
}

/* --- method 2 ----------------------------------------------------------- */

static void UpdateTmpCrcData(Vars *v, uint8_t b)
{
    if (v->bitCount != 0)
        v->tmpCrcData[v->v11++] = b;
    else
        WriteToOutput(v, b);
}

static void EncodeMatchesCount(Vars *v, int32_t count)
{
    int32_t i;
    while (count > 0) {
        if (count >= 12) {
            if (count & 3) {
                WriteBitsM2(v, 0, 1);
                UpdateTmpCrcData(v, (uint8_t)(v->encKey ^ ReadFromInput(v)));
                count--;
            } else {
                WriteBitsM2(v, 0x17, 5);
                if (count >= 72) {
                    WriteBitsM2(v, 0xF, 4);
                    for (i = 0; i < 72; ++i)
                        UpdateTmpCrcData(v, (uint8_t)(v->encKey ^ ReadFromInput(v)));
                    count -= 72;
                } else {
                    WriteBitsM2(v, (uint16_t)((count - 12) >> 2), 4);
                    while (count-- > 0)
                        UpdateTmpCrcData(v, (uint8_t)(v->encKey ^ ReadFromInput(v)));
                }
            }
            RorW(&v->encKey);
        } else {
            while (count != 0) {
                WriteBitsM2(v, 0, 1);
                UpdateTmpCrcData(v, (uint8_t)(v->encKey ^ ReadFromInput(v)));
                RorW(&v->encKey);
                count--;
            }
        }
    }
}

static void CompressData2(Vars *v)
{
    int32_t srcOffset = 0, i;

    while (v->v7 < v->unpackedSize) {
        Proc6(v);
        v->inputOffset = srcOffset;

        while (v->v17-- > 0) {
            uint32_t dataLength = ReadWordBE(v->temp, &v->tempOffset);
            v->v7 += (int32_t)dataLength;
            EncodeMatchesCount(v, (int32_t)dataLength);

            if (v->v17 > 0) {
                v->matchCount = ReadWordBE(v->temp, &v->tempOffset);
                v->matchOffset = ReadWordBE(v->temp, &v->tempOffset);

                if (v->matchCount != 0) {
                    if (v->matchCount >= 7) {
                        WriteBitsM2(v, 0xF, 4);
                        UpdateTmpCrcData(v, (uint8_t)(v->matchCount - 6));
                    } else {
                        WriteBitsM2(v, kMatchCountBits[v->matchCount], kMatchCountBitsCount[v->matchCount]);
                    }
                    WriteBitsM2(v, kMatchOffsetBits[v->matchOffset >> 8],
                                kMatchOffsetBitsCount[v->matchOffset >> 8]);
                } else {
                    WriteBitsM2(v, 6, 3);
                }
                UpdateTmpCrcData(v, (uint8_t)v->matchOffset);

                v->matchCount = (uint16_t)(v->matchCount + 2);
                v->v7 += v->matchCount;
                while (v->matchCount > 0) {
                    ReadFromInput(v);
                    v->matchCount--;
                }
            }
        }

        WriteBitsM2(v, 0xF, 4);
        UpdateTmpCrcData(v, 0);
        WriteBitsM2(v, (uint16_t)(v->v7 >= v->unpackedSize ? 0 : 1), 1);

        if (v->bitCount == 0) {
            for (i = 0; i < v->v11; ++i)
                WriteToOutput(v, v->tmpCrcData[i]);
            v->v11 = 0;
        }
        v->chunksCount++;
        srcOffset = v->inputOffset;
    }

    v->packToken = (uint16_t)(v->packToken << (8 - v->bitCount));
    if (v->bitCount != 0 || v->v11 != 0)
        WriteToOutput(v, (uint8_t)v->packToken);
}

/* --- method 1: Huffman tables ------------------------------------------- */

static void ClearTable(Huftable *data, int32_t count)
{
    int32_t i;
    for (i = 0; i < count; ++i) {
        data[i].l1 = 0;
        data[i].l2 = 0xFFFF;
        data[i].l3 = 0;
        data[i].bitDepth = 0;
    }
}

/* The two least frequent symbols into v20, v21; 1 when there are two. */
static int32_t Proc17(Vars *v, const Huftable *data, int32_t count)
{
    uint32_t d6 = 0xFFFFFFFFu, d5 = 0xFFFFFFFFu;
    int32_t i;
    for (i = 0; i < count; ++i) {
        if (data[i].l1 != 0) {
            if (data[i].l1 < d5) {
                d6 = d5;
                v->v21 = v->v20;
                d5 = data[i].l1;
                v->v20 = i;
            } else if (data[i].l1 < d6) {
                d6 = data[i].l1;
                v->v21 = i;
            }
        }
    }
    return (d5 != 0xFFFFFFFFu && d6 != 0xFFFFFFFFu) ? 1 : 0;
}

static uint32_t InverseBits(uint32_t value, int32_t count)
{
    uint32_t i = 0;
    while (count-- > 0) {
        i <<= 1;
        if (value & 1)
            i |= 1;
        value >>= 1;
    }
    return i;
}

/* Canonical codes from the depths, bit-reversed for the LSB-first stream. */
static void Proc20(Huftable *data, int32_t count)
{
    uint32_t val = 0, div = 0x80000000u;
    int32_t bitCount, i;
    for (bitCount = 1; bitCount <= 16; ++bitCount, div >>= 1) {
        for (i = 0; i < count; ++i) {
            if (data[i].bitDepth == bitCount) {
                data[i].l3 = InverseBits(val / div, bitCount);
                val += div;
            }
        }
    }
}

static void Proc16(Vars *v, Huftable *data, int32_t count)
{
    int32_t d4 = 0, ve = 0, i;
    for (i = 0; i < count; ++i) {
        if (data[i].l1 != 0) {
            d4++;
            ve = i;
        }
    }
    if (d4 == 0)
        return;
    if (d4 == 1) {
        data[ve].bitDepth++;
        return;
    }
    while (Proc17(v, data, count)) {
        data[v->v20].l1 += data[v->v21].l1;
        data[v->v21].l1 = 0;
        data[v->v20].bitDepth++;
        while (data[v->v20].l2 != 0xFFFF) {
            v->v20 = data[v->v20].l2;
            data[v->v20].bitDepth++;
        }
        data[v->v20].l2 = (uint16_t)v->v21;
        data[v->v21].bitDepth++;
        while (data[v->v21].l2 != 0xFFFF) {
            v->v21 = data[v->v21].l2;
            data[v->v21].bitDepth++;
        }
    }
    Proc20(data, count);
}

static void Proc18(Vars *v, const Huftable *data, int32_t count)
{
    int32_t i;
    while (count > 0 && data[count - 1].bitDepth == 0)
        count--;
    WriteBitsM1(v, (uint16_t)count, 5);
    for (i = 0; i < count; ++i)
        WriteBitsM1(v, data[i].bitDepth, 4);
}

static void Proc19(Vars *v, const Huftable *data, int32_t count)
{
    int32_t bits = count > 1 ? BitsCount(count) : count;
    WriteBitsM1(v, (uint16_t)data[bits].l3, data[bits].bitDepth);
    if (bits > 1)
        WriteBitsM1(v, (uint16_t)(count - (1 << (bits - 1))), bits - 1);
}

static void CompressData1(Vars *v)
{
    int32_t srcOffset = 0, i;

    while (v->v7 < v->unpackedSize) {
        ClearTable(v->lenTable, 16);
        ClearTable(v->posTable, 16);
        ClearTable(v->rawTable, 16);

        Proc6(v);
        v->inputOffset = srcOffset;

        Proc16(v, v->rawTable, 16);
        Proc16(v, v->lenTable, 16);
        Proc16(v, v->posTable, 16);

        Proc18(v, v->rawTable, 16);
        Proc18(v, v->lenTable, 16);
        Proc18(v, v->posTable, 16);

        WriteBitsM1(v, (uint16_t)v->v17, 16);

        while (v->v17 > 0) {
            uint32_t dataLength;
            v->v17--;

            dataLength = ReadWordBE(v->temp, &v->tempOffset);
            v->v7 += (int32_t)dataLength;
            Proc19(v, v->rawTable, (int32_t)dataLength);

            if (dataLength != 0) {
                while (dataLength-- > 0) {
                    uint8_t x = (uint8_t)(v->encKey ^ ReadFromInput(v));
                    if (v->bitCount == 0)
                        WriteToOutput(v, x);
                    else
                        v->tmpCrcData[v->v11++] = x;
                }
                RorW(&v->encKey);
            }

            if (v->v17 > 0) {
                v->matchCount = ReadWordBE(v->temp, &v->tempOffset);
                v->matchOffset = ReadWordBE(v->temp, &v->tempOffset);
                Proc19(v, v->lenTable, v->matchOffset);
                Proc19(v, v->posTable, v->matchCount);

                v->matchCount = (uint16_t)(v->matchCount + 2);
                v->v7 += v->matchCount;
                while (v->matchCount-- > 0)
                    ReadFromInput(v);
            }
        }

        if (v->bitCount == 0) {
            for (i = 0; i < v->v11; ++i)
                WriteToOutput(v, v->tmpCrcData[i]);
            v->v11 = 0;
        }
        v->chunksCount++;
        srcOffset = v->inputOffset;
    }

    v->packToken = (uint16_t)(v->packToken >> (16 - v->bitCount));
    if (v->bitCount != 0 || v->v11 != 0)
        WriteToOutput(v, (uint8_t)v->packToken);
    if (v->bitCount > 8 || v->v11 != 0)
        WriteToOutput(v, (uint8_t)(v->packToken >> 8));
}

/* --- DoPackData --------------------------------------------------------- */

static void DoPackData(Vars *v)
{
    uint16_t key;
    int32_t i, off;

    v->unpackedSize = v->fileSize;
    v->bytesLeft = v->fileSize;

    WriteDwordBE(v->output, &v->outputOffset, (0x524E43u << 8) | (uint32_t)(v->method & 0xFF));
    WriteDwordBE(v->output, &v->outputOffset, (uint32_t)v->unpackedSize);
    WriteDwordBE(v->output, &v->outputOffset, 0);
    WriteWordBE(v->output, &v->outputOffset, 0);
    WriteWordBE(v->output, &v->outputOffset, 0);
    WriteWordBE(v->output, &v->outputOffset, 0);

    InitDicts(v);

    key = v->encKey;
    WriteBits(v, 0, 1);                             /* not locked */
    WriteBits(v, (uint16_t)(v->encKey != 0), 1);    /* not encrypted */

    if (v->method == 1)
        CompressData1(v);
    else
        CompressData2(v);

    for (i = 0; i < v->v11; ++i)
        WriteToOutput(v, v->tmpCrcData[i]);
    v->v11 = 0;
    v->encKey = key;

    if (v->leeway > v->unpackedSize - v->packedSize)
        v->leeway -= v->unpackedSize - v->packedSize;
    else
        v->leeway = 0;
    if (v->method == 2)
        v->leeway += 2;

    v->packedSize = v->outputOffset;
    off = 8;
    WriteDwordBE(v->output, &off, (uint32_t)(v->packedSize - RNC_HEADER_SIZE));
    WriteWordBE(v->output, &off, v->unpackedCrc);
    WriteWordBE(v->output, &off, v->packedCrc);
    WriteByte(v->output, &off, (uint8_t)v->leeway);
    WriteByte(v->output, &off, (uint8_t)v->chunksCount);
}

int32_t Sg_RncPackEx(const SgBuf *in, int32_t method, uint16_t dictSize, SgBuf *out, char *err)
{
    Vars *v;
    int32_t rc = 0;

    out->data = NULL;
    out->size = 0;
    if (method != 1 && method != 2) {
        snprintf(err, 256, "RNC method %d: only 1 and 2 exist", (int)method);
        return -1;
    }
    if (in->size <= RNC_HEADER_SIZE) {
        snprintf(err, 256, "too small to RNC-pack (%u bytes)", (unsigned)in->size);
        return -1;
    }
    if (in->size > 0x7FFFFFF0u) {
        snprintf(err, 256, "too large to RNC-pack (%u bytes)", (unsigned)in->size);
        return -1;
    }
    if (in->data[0] == 'R' && in->data[1] == 'N' && in->data[2] == 'C') {
        snprintf(err, 256, "already RNC-packed");
        return -1;
    }

    v = (Vars *)calloc(1, sizeof *v);
    if (!v) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    v->method = method;
    v->packBlockSize = 0x3000;
    v->encKey = 0;
    if (method == 1) {
        v->dictSize = (dictSize == 0 || dictSize > 0x8000) ? 0x8000 : dictSize;
        v->maxMatches = 0x1000;
    } else {
        v->dictSize = (dictSize == 0 || dictSize > 0x1000) ? 0x1000 : dictSize;
        v->maxMatches = 0xFF;
    }
    v->fileSize = (int32_t)in->size;
    v->input = in->data;

    /* Zeroed, as the C# arrays are: the window is read a little past its fill. */
    v->mem1 = (uint8_t *)calloc(MEM1_SIZE + 16, 1);
    v->mem2 = (uint16_t *)calloc(HASH_SIZE, sizeof(uint16_t));
    v->mem3 = (uint16_t *)calloc(HASH_SIZE, sizeof(uint16_t));
    v->mem4 = (uint16_t *)calloc(HASH_SIZE, sizeof(uint16_t));
    v->mem5 = (uint16_t *)calloc(HASH_SIZE, sizeof(uint16_t));
    v->temp = (uint8_t *)calloc(TEMP_SIZE, 1);
    /* WriteToOutput stops at fileSize - 18 bytes of packed data, plus the header. */
    v->output = (uint8_t *)calloc((size_t)v->fileSize + 32, 1);

    if (!v->mem1 || !v->mem2 || !v->mem3 || !v->mem4 || !v->mem5 || !v->temp || !v->output) {
        snprintf(err, 256, "out of memory");
        rc = -1;
    } else {
        DoPackData(v);
        if (v->overflow) {
            snprintf(err, 256, "does not RNC-pack smaller than its %u bytes", (unsigned)in->size);
            rc = -1;
        } else {
            out->data = v->output;
            out->size = (uint32_t)v->packedSize;
            v->output = NULL;
        }
    }

    free(v->mem1);
    free(v->mem2);
    free(v->mem3);
    free(v->mem4);
    free(v->mem5);
    free(v->temp);
    free(v->output);
    free(v);
    return rc;
}

int32_t Sg_RncPack(const SgBuf *in, int32_t method, SgBuf *out, char *err)
{
    return Sg_RncPackEx(in, method, 0, out, err);
}

int32_t Sg_RncPackLikeSwosGfx(const SgBuf *in, SgBuf *out, char *err)
{
    return Sg_RncPackEx(in, SG_RNC_SWOSGFX_METHOD, 0, out, err);
}
