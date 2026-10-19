/*
 * rnc_unpack.c -- RNC ProPack unpacking (methods 1 and 2); the format is
 * described in rnc_pack.h.
 *
 * A line-for-line port of SwosGfx's RncProcessor.cs unpack path (DoUnpack,
 * DoUnpackData, UnpackDataM1/M2, InputBitsM1/M2, DecodeMatchCount/Offset,
 * MakeHufTable, DecodeTableData, WriteDecodedByte), which ports lab313ru's
 * rnc_propack_source. The names are kept so the two can be read side by side.
 * Every ushort of the C# is a uint16_t here, so the wraps are the same.
 *
 * What differs, all of it for input that is not a well-formed RNC file (the
 * input comes from users, so a damaged file must fail cleanly):
 *
 *  - The C# reads the bit stream through a 0xFFFF-byte block buffer (Mem1,
 *    refilled 0xFFFD bytes at a time with two bytes of look-ahead) and peeks up
 *    to three bytes past its read position. Here the stream is read straight
 *    from the input, which gives the same bytes, without the C#'s read of
 *    Mem1[0xFFFF] (one past its end) when a method-1 literal run ends exactly at
 *    a block boundary. Bytes past the packed data read as 0, and reading more
 *    than a few of them fails the unpack: the stream is truncated.
 *  - The decoded output never grows past the header's unpacked size, a
 *    Huffman code that matches no table entry fails (the C# runs off the end of
 *    its table), the packed size must fit in the file (the C# only compares it
 *    with the whole file, header included), and a method that is neither 1 nor
 *    2 is refused (the C# returns an empty output).
 *  - The unpacked size must equal what the stream decoded to, and both CRCs
 *    must match, as in the C#.
 */
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RNC_HEADER_SIZE 0x12
#define MAX_BUF_SIZE    0x1E00000       /* the C#'s limit on either size, 30 MB */
#define WINDOW_SIZE     0xFFFF          /* Decoded: dictionary + what is not flushed yet */
#define READ_SLACK      4               /* bytes past the packed data a stream may peek at */

/* CRC-16, poly 0xA001 reflected, init 0: the same table as rnc_pack.c's
 * kCrcTable, built from the polynomial on first use. */
static uint16_t gCrcTable[256];
static int32_t  gCrcTableBuilt;

static void BuildCrcTable(void)
{
    int32_t i, bit;
    if (gCrcTableBuilt)
        return;
    for (i = 0; i < 256; ++i) {
        uint16_t c = (uint16_t)i;
        for (bit = 0; bit < 8; ++bit)
            c = (uint16_t)((c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1));
        gCrcTable[i] = c;
    }
    gCrcTableBuilt = 1;
}

typedef struct {
    uint32_t l1;        /* frequency (unused when unpacking) */
    uint16_t l2;
    uint32_t l3;        /* code, bit-reversed for the LSB-first stream */
    uint16_t bitDepth;
} Huftable;

typedef struct {
    uint16_t encKey, dictSize;
    int32_t  method;

    int32_t  inputSize;         /* unpacked size from the header */
    int32_t  packedSize;        /* packed size from the header */
    int32_t  processedSize;
    uint16_t bitCount;
    uint16_t matchCount, matchOffset;
    uint32_t bitBuffer;

    uint16_t unpackedCrc, unpackedCrcReal, packedCrc;

    const uint8_t *input;       /* the packed data, after the header */
    int32_t  inputOffset;       /* next byte to read: the C#'s Mem1[PackBlockStartIndex] */
    int32_t  inputEnd;          /* = packedSize */

    uint8_t  *decoded;          /* WINDOW_SIZE bytes */
    int32_t  windowIndex;

    uint8_t  *output;           /* inputSize bytes */
    int32_t  outputOffset;

    Huftable rawTable[16], posTable[16], lenTable[16];

    const char *error;          /* set once: the first thing that went wrong */
} Vars;

static void Fail(Vars *v, const char *why)
{
    if (!v->error)
        v->error = why;
}

static uint16_t CrcBlock(const uint8_t *buf, int32_t size)
{
    uint16_t crc = 0;
    while (size-- > 0) {
        crc ^= *buf++;
        crc = (uint16_t)((crc >> 8) ^ gCrcTable[crc & 0xFF]);
    }
    return crc;
}

static void RorW(uint16_t *x)
{
    *x = (uint16_t)((*x & 1) ? (0x8000 | (*x >> 1)) : (*x >> 1));
}

/* The byte `ahead` bytes past the read position, 0 past the packed data. */
static uint8_t PeekSourceByte(const Vars *v, int32_t ahead)
{
    int32_t i = v->inputOffset + ahead;
    return i < v->inputEnd ? v->input[i] : 0;
}

static uint8_t ReadSourceByte(Vars *v)
{
    uint8_t b = PeekSourceByte(v, 0);
    if (v->inputOffset >= v->inputEnd + READ_SLACK)
        Fail(v, "the packed data ends too soon (truncated or damaged)");
    else
        v->inputOffset++;
    return b;
}

static uint32_t InputBitsM2(Vars *v, int16_t count)
{
    uint32_t bits = 0;

    while (count-- > 0) {
        if (v->bitCount == 0) {
            v->bitBuffer = ReadSourceByte(v);
            v->bitCount = 8;
        }

        bits <<= 1;

        if (v->bitBuffer & 0x80)
            bits |= 1;

        v->bitBuffer <<= 1;
        v->bitCount--;
    }

    return bits;
}

static uint32_t InputBitsM1(Vars *v, int16_t count)
{
    uint32_t bits = 0;
    uint32_t prevBits = 1;

    while (count-- > 0) {
        if (v->bitCount == 0) {
            uint8_t b1 = ReadSourceByte(v);
            uint8_t b2 = ReadSourceByte(v);

            /* The next word is already in the top half: the C# peeks Mem1. */
            v->bitBuffer = ((uint32_t)PeekSourceByte(v, 1) << 24) |
                           ((uint32_t)PeekSourceByte(v, 0) << 16) |
                           ((uint32_t)b2 << 8) |
                           b1;

            v->bitCount = 16;
        }

        if (v->bitBuffer & 1)
            bits |= prevBits;

        v->bitBuffer >>= 1;
        prevBits <<= 1;
        v->bitCount--;
    }

    return bits;
}

static int32_t InputBits(Vars *v, int16_t count)
{
    if (v->method != 2)
        return (int32_t)InputBitsM1(v, count);
    else
        return (int32_t)InputBitsM2(v, count);
}

static void DecodeMatchCount(Vars *v)
{
    v->matchCount = (uint16_t)(InputBitsM2(v, 1) + 4);

    if (InputBitsM2(v, 1) != 0)
        v->matchCount = (uint16_t)(((v->matchCount - 1) << 1) + InputBitsM2(v, 1));
}

static void DecodeMatchOffset(Vars *v)
{
    v->matchOffset = 0;
    if (InputBitsM2(v, 1) != 0) {
        v->matchOffset = (uint16_t)InputBitsM2(v, 1);

        if (InputBitsM2(v, 1) != 0) {
            v->matchOffset = (uint16_t)(((v->matchOffset << 1) | InputBitsM2(v, 1)) | 4);

            if (InputBitsM2(v, 1) == 0)
                v->matchOffset = (uint16_t)((v->matchOffset << 1) | InputBitsM2(v, 1));
        } else if (v->matchOffset == 0)
            v->matchOffset = (uint16_t)(InputBitsM2(v, 1) + 2);
    }

    v->matchOffset = (uint16_t)(((v->matchOffset << 8) | ReadSourceByte(v)) + 1);
}

/* Copy n bytes of the window to the output. Never more than inputSize in all:
 * WriteDecodedByte stops there. */
static void WriteBuf(Vars *v, int32_t from, int32_t n)
{
    memcpy(v->output + v->outputOffset, v->decoded + from, (size_t)n);
    v->outputOffset += n;
}

static void WriteDecodedByte(Vars *v, uint8_t b)
{
    /* Everything the window holds counts: flushed + window - dictionary. */
    if (v->outputOffset + (v->windowIndex - v->dictSize) >= v->inputSize) {
        Fail(v, "the packed data decodes to more than the header's unpacked size");
        return;
    }

    if (v->windowIndex == WINDOW_SIZE) {
        WriteBuf(v, v->dictSize, WINDOW_SIZE - v->dictSize);
        memmove(v->decoded, v->decoded + v->windowIndex - v->dictSize, v->dictSize);
        v->windowIndex = v->dictSize;
    }

    v->decoded[v->windowIndex++] = b;
    v->unpackedCrcReal = (uint16_t)(gCrcTable[(v->unpackedCrcReal ^ b) & 0xFF] ^ (v->unpackedCrcReal >> 8));
}

/* A match: offset <= 0x1000 (method 2) or 0x8000 (method 1), never more than
 * the dictionary, and windowIndex >= dictSize, so the read stays in the window;
 * before the first byte it reads the zeroed dictionary, as the C# does. */
static void CopyMatch(Vars *v)
{
    while (v->matchCount-- > 0 && !v->error)
        WriteDecodedByte(v, v->decoded[v->windowIndex - v->matchOffset]);
}

static int32_t UnpackDataM2(Vars *v)
{
    while (v->processedSize < v->inputSize && !v->error) {
        while (!v->error) {
            if (InputBitsM2(v, 1) == 0) {
                WriteDecodedByte(v, (uint8_t)((v->encKey ^ ReadSourceByte(v)) & 0xFF));
                RorW(&v->encKey);
                v->processedSize++;
            } else {
                if (InputBitsM2(v, 1) != 0) {
                    if (InputBitsM2(v, 1) != 0) {
                        if (InputBitsM2(v, 1) != 0) {
                            v->matchCount = (uint16_t)(ReadSourceByte(v) + 8);

                            if (v->matchCount == 8) {   /* end of chunk */
                                InputBitsM2(v, 1);
                                break;
                            }
                        } else
                            v->matchCount = 3;

                        DecodeMatchOffset(v);
                    } else {
                        v->matchCount = 2;
                        v->matchOffset = (uint16_t)(ReadSourceByte(v) + 1);
                    }

                    v->processedSize += v->matchCount;
                    CopyMatch(v);
                } else {
                    DecodeMatchCount(v);

                    if (v->matchCount != 9) {
                        DecodeMatchOffset(v);
                        v->processedSize += v->matchCount;
                        CopyMatch(v);
                    } else {
                        uint32_t dataLength = (InputBitsM2(v, 4) << 2) + 12;
                        v->processedSize += (int32_t)dataLength;

                        while (dataLength-- > 0 && !v->error)
                            WriteDecodedByte(v, (uint8_t)((v->encKey ^ ReadSourceByte(v)) & 0xFF));

                        RorW(&v->encKey);
                    }
                }
            }
        }
    }

    if (v->error)
        return -1;
    WriteBuf(v, v->dictSize, v->windowIndex - v->dictSize);
    return 0;
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

/* Canonical codes from the depths, bit-reversed for the LSB-first stream
 * (rnc_pack.c has the same). */
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

static void MakeHufTable(Vars *v, Huftable *data, int32_t count)
{
    int32_t leafNodes, i;

    ClearTable(data, count);

    leafNodes = (int32_t)InputBitsM1(v, 5);

    if (leafNodes != 0) {
        if (leafNodes > 16)
            leafNodes = 16;

        for (i = 0; i < leafNodes; ++i)
            data[i].bitDepth = (uint16_t)InputBitsM1(v, 4);

        Proc20(data, leafNodes);
    }
}

static uint32_t DecodeTableData(Vars *v, const Huftable *data)
{
    uint32_t i;

    for (i = 0; i < 16; ++i) {
        if (data[i].bitDepth != 0 &&
            data[i].l3 == (v->bitBuffer & ((1u << data[i].bitDepth) - 1))) {
            InputBitsM1(v, (int16_t)data[i].bitDepth);

            if (i < 2)
                return i;

            return InputBitsM1(v, (int16_t)(i - 1)) | (1u << (i - 1));
        }
    }

    /* The C# keeps looking past the table's end. */
    Fail(v, "a Huffman code matches no table entry (damaged data)");
    return 0;
}

static int32_t UnpackDataM1(Vars *v)
{
    while (v->processedSize < v->inputSize && !v->error) {
        int32_t subChunks;

        MakeHufTable(v, v->rawTable, 16);
        MakeHufTable(v, v->lenTable, 16);
        MakeHufTable(v, v->posTable, 16);

        subChunks = (int32_t)InputBitsM1(v, 16);

        while (subChunks-- > 0 && !v->error) {
            uint32_t dataLength = DecodeTableData(v, v->rawTable);
            v->processedSize += (int32_t)dataLength;

            if (dataLength != 0) {
                while (dataLength-- > 0 && !v->error)
                    WriteDecodedByte(v, (uint8_t)((v->encKey ^ ReadSourceByte(v)) & 0xFF));

                RorW(&v->encKey);

                /* The literals came out of the middle of the bit stream: refill
                 * the buffer above the bits still held. */
                v->bitBuffer =
                    ((((uint32_t)PeekSourceByte(v, 2) << 16) |
                      ((uint32_t)PeekSourceByte(v, 1) << 8) |
                      PeekSourceByte(v, 0)) << v->bitCount) |
                    (v->bitBuffer & ((1u << v->bitCount) - 1));
            }

            if (subChunks > 0) {
                v->matchOffset = (uint16_t)(DecodeTableData(v, v->lenTable) + 1);
                v->matchCount = (uint16_t)(DecodeTableData(v, v->posTable) + 2);
                v->processedSize += v->matchCount;
                CopyMatch(v);
            }
        }
    }

    if (v->error)
        return -1;
    WriteBuf(v, v->dictSize, v->windowIndex - v->dictSize);
    return 0;
}

/* DoUnpack + DoUnpackData. */
int32_t Sg_RncUnpack(const SgBuf *in, SgBuf *out, char *err)
{
    Vars *v;
    const uint8_t *p;
    uint32_t sign, inputSize, packedSize;
    int32_t rc;

    out->data = NULL;
    out->size = 0;

    if (!Sg_IsRnc(in)) {
        out->data = malloc(in->size ? in->size : 1);
        if (!out->data) {
            snprintf(err, 256, "out of memory");
            return -1;
        }
        if (in->size)
            memcpy(out->data, in->data, in->size);
        out->size = in->size;
        return 0;
    }

    BuildCrcTable();
    p = in->data;

    sign = Sg_Be32(p);
    inputSize = Sg_Be32(p + 4);
    packedSize = Sg_Be32(p + 8);

    if (inputSize > MAX_BUF_SIZE) {
        snprintf(err, 256, "RNC: the header's unpacked size (%u) is too large", (unsigned)inputSize);
        return -1;
    }
    if (packedSize > in->size - RNC_HEADER_SIZE) {
        snprintf(err, 256, "RNC: the header's packed size (%u) is more than the file holds (%u) -- truncated?",
                 (unsigned)packedSize, (unsigned)(in->size - RNC_HEADER_SIZE));
        return -1;
    }

    v = (Vars *)calloc(1, sizeof(Vars));
    if (!v) {
        snprintf(err, 256, "out of memory");
        return -1;
    }

    v->method = (int32_t)(sign & 3);    /* 1 or 2: Sg_IsRnc checked */
    v->inputSize = (int32_t)inputSize;
    v->packedSize = (int32_t)packedSize;
    v->unpackedCrc = Sg_Be16(p + 12);
    v->packedCrc = Sg_Be16(p + 14);
    /* p[16] leeway and p[17] chunk count are not needed to unpack. */

    v->input = p + RNC_HEADER_SIZE;
    v->inputEnd = v->packedSize;

    if (CrcBlock(v->input, v->packedSize) != v->packedCrc) {
        free(v);
        snprintf(err, 256, "RNC: the packed data's CRC does not match (damaged file)");
        return -1;
    }

    /* The C#'s default dictionary (0xFFFF) clamped per method. */
    v->dictSize = v->method == 1 ? 0x8000 : 0x1000;

    v->decoded = (uint8_t *)calloc(WINDOW_SIZE, 1);
    v->output = (uint8_t *)malloc(v->inputSize ? (size_t)v->inputSize : 1);
    if (!v->decoded || !v->output) {
        free(v->decoded);
        free(v->output);
        free(v);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    v->windowIndex = v->dictSize;
    v->encKey = 0;

    rc = 0;
    InputBits(v, 1);                    /* locked: means nothing to an unpacker */
    if (InputBits(v, 1) != 0 && v->encKey == 0) {
        Fail(v, "the file is encrypted (needs a key)");
        rc = -1;
    }

    if (rc == 0)
        rc = v->method == 1 ? UnpackDataM1(v) : UnpackDataM2(v);

    if (rc == 0 && v->outputOffset != v->inputSize)
        Fail(v, "the packed data decodes to less than the header's unpacked size");
    if (!v->error && v->unpackedCrc != v->unpackedCrcReal)
        Fail(v, "the unpacked data's CRC does not match (damaged file)");

    free(v->decoded);
    if (v->error) {
        snprintf(err, 256, "RNC method %d: %s", (int)v->method, v->error);
        free(v->output);
        free(v);
        return -1;
    }

    out->data = v->output;
    out->size = (uint32_t)v->inputSize;
    free(v);
    return 0;
}
