/*
 * jag2amiga.c -- the Amiga sprite banks from the Jaguar cartridge (see jag2amiga.h).
 *
 * Ported from the C# SwosGfx's JagToAmiga.cs -- the bank table, the placement
 * walk, JagRom's depacker and CRY reader -- with its CJCBENCH patch files
 * replaced by the code that derives them.
 */
#include "jag2amiga.h"

#include "amiga_raw.h"
#include "dos2amiga.h"
#include "dos_sprite.h"
#include "sg_dir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const Sg_JagBankNames[SG_JAG_BANK_COUNT] = {
    "CJCTEAM1", "CJCTEAM2", "CJCTEAM3", "CJCTEAMG", "CJCBITS", "CJCBENCH",
};

#define CART_BASE          0x800000u
#define GAME_PALETTE_OFS   0x6FA42u
#define SHEET_SIZE         (SG_BANK_W * SG_BANK_H)

/* ---- CRY chroma tables (Atari's, as JagToAmiga.cs / jagfx carry them) --- */

static const uint8_t kCryR[256] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x13, 0x00,
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x40, 0x2B, 0x15, 0x00,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x5F, 0x47, 0x2F, 0x17, 0x00,
    0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x68, 0x4E, 0x34, 0x1A, 0x00,
    0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xAA, 0x8D, 0x71, 0x55, 0x38, 0x1C, 0x00,
    0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0x99, 0x7A, 0x5B, 0x3D, 0x1E, 0x00,
    0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xE6, 0xC5, 0xA4, 0x83, 0x62, 0x41, 0x20, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xD6, 0xB5, 0x94, 0x73, 0x52, 0x31, 0x11,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0xCC, 0xAD, 0x8F, 0x70, 0x51, 0x33,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC6, 0xAA, 0x8D, 0x71, 0x55,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF9, 0xDF, 0xC5, 0xAB, 0x91, 0x77,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xE0, 0xC8, 0xB1, 0x99,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xE6, 0xD0, 0xBB,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xDD,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static const uint8_t kCryG[256] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0x00, 0x13, 0x26, 0x39, 0x4D, 0x60, 0x73, 0x86, 0x9A, 0xAD, 0xC0, 0xD3, 0xE7, 0xFA, 0xFF, 0xFF,
    0x00, 0x15, 0x2B, 0x40, 0x56, 0x6B, 0x81, 0x96, 0xAC, 0xC1, 0xD7, 0xEC, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x17, 0x2F, 0x47, 0x5F, 0x77, 0x8E, 0xA6, 0xBE, 0xD6, 0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1A, 0x34, 0x4E, 0x68, 0x82, 0x9C, 0xB6, 0xD0, 0xEA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1C, 0x38, 0x55, 0x71, 0x8D, 0xAA, 0xC6, 0xE2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1E, 0x3D, 0x5B, 0x85, 0x99, 0xB7, 0xD6, 0xF4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x20, 0x41, 0x62, 0x83, 0xA4, 0xC5, 0xE6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x20, 0x41, 0x62, 0x83, 0xA4, 0xC5, 0xE6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1E, 0x3D, 0x5B, 0x85, 0x99, 0xB7, 0xD6, 0xF4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1C, 0x38, 0x55, 0x71, 0x8D, 0xAA, 0xC6, 0xE2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x1A, 0x34, 0x4E, 0x68, 0x82, 0x9C, 0xB6, 0xD0, 0xEA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x17, 0x2F, 0x47, 0x5F, 0x77, 0x8E, 0xA6, 0xBE, 0xD6, 0xEE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x15, 0x2B, 0x40, 0x56, 0x6B, 0x81, 0x96, 0xAC, 0xC1, 0xD7, 0xEC, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x13, 0x26, 0x39, 0x4D, 0x60, 0x73, 0x86, 0x9A, 0xAD, 0xC0, 0xD3, 0xE7, 0xFA, 0xFF, 0xFF,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
};

static const uint8_t kCryB[256] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xDD,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0xE6, 0xD0, 0xBB,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xE0, 0xC8, 0xB1, 0x99,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF9, 0xDF, 0xC5, 0xAB, 0x91, 0x77,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE3, 0xC6, 0xAA, 0x8D, 0x71, 0x55,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xEB, 0xCC, 0xAD, 0x8F, 0x70, 0x51, 0x33,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0xD6, 0xB5, 0x94, 0x73, 0x52, 0x31, 0x11,
    0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xED, 0xE6, 0xC5, 0xA4, 0x83, 0x62, 0x41, 0x20, 0x00,
    0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0xCB, 0x99, 0x7A, 0x5B, 0x3D, 0x1E, 0x00,
    0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xA9, 0xAA, 0x8D, 0x71, 0x55, 0x38, 0x1C, 0x00,
    0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x68, 0x4E, 0x34, 0x1A, 0x00,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x5F, 0x47, 0x2F, 0x17, 0x00,
    0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x40, 0x2B, 0x15, 0x00,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x13, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ---- the cartridge ------------------------------------------------------ */

static int32_t ToFileOffset(const SgBuf *rom, uint32_t address, uint32_t *ofs, char *err)
{
    uint32_t o = address >= CART_BASE ? address - CART_BASE : address;

    if (o >= rom->size) {
        snprintf(err, 256, "address $%06X is outside the %u byte cartridge image",
                 (unsigned)address, (unsigned)rom->size);
        return -1;
    }
    *ofs = o;
    return 0;
}

/* JagRom.LzBits: stop at `size` bytes, at the end-of-stream code, or when the
 * image runs out -- the last two are short and an error for us, as they are
 * for SwosGfx's UnpackSheet. */
int32_t Sg_JagUnpack(const SgBuf *rom, uint32_t address, uint32_t size, SgBuf *out, char *err)
{
    enum { WINDOW = 8192, MASK = WINDOW - 1, OFFSET_BITS = 13, LENGTH_BITS = 4, MIN_MATCH = 3 };
    uint8_t ring[WINDOW];
    uint32_t start, outLen = 0, wpos = 1;
    uint64_t bitPos, bitEnd;
    const uint8_t *src = rom->data;

    out->data = NULL;
    out->size = 0;
    if (ToFileOffset(rom, address, &start, err) != 0)
        return -1;
    out->data = malloc(size ? size : 1);
    if (!out->data) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    memset(ring, 0, sizeof ring);
    bitPos = (uint64_t)start * 8;
    bitEnd = (uint64_t)rom->size * 8;

#define READ_BITS(n, v)                                                       \
    do {                                                                      \
        (v) = 0;                                                              \
        for (int32_t i_ = 0; i_ < (n); i_++, bitPos++)                        \
            (v) = ((v) << 1) | ((src[bitPos >> 3] >> (7 - (bitPos & 7))) & 1); \
    } while (0)

    while (outLen < size && bitPos + 1 + 8 <= bitEnd) {
        uint32_t flag, offset, length;

        READ_BITS(1, flag);
        if (flag) {
            uint32_t lit;
            READ_BITS(8, lit);
            out->data[outLen++] = (uint8_t)lit;
            ring[wpos] = (uint8_t)lit;
            wpos = (wpos + 1) & MASK;
            continue;
        }
        if (bitPos + OFFSET_BITS + LENGTH_BITS > bitEnd)
            break;
        READ_BITS(OFFSET_BITS, offset);
        READ_BITS(LENGTH_BITS, length);
        length += MIN_MATCH;
        if (offset == 0)
            break;                                  /* end of stream */
        for (uint32_t i = 0; i < length && outLen < size; i++) {
            uint8_t c = ring[offset];
            out->data[outLen++] = c;
            ring[wpos] = c;
            wpos = (wpos + 1) & MASK;
            offset = (offset + 1) & MASK;
        }
    }
#undef READ_BITS

    if (outLen != size) {
        snprintf(err, 256, "Jaguar stream at $%06X unpacked to %u bytes, expected %u",
                 (unsigned)address, (unsigned)outLen, (unsigned)size);
        Sg_BufFree(out);
        return -1;
    }
    out->size = size;
    return 0;
}

int32_t Sg_JagGamePalette(const SgBuf *rom, uint32_t pal[256], char *err)
{
    if (rom->size < GAME_PALETTE_OFS + 512) {
        snprintf(err, 256, "the cartridge image is too short for its palette");
        return -1;
    }
    for (int32_t i = 0; i < 256; i++) {
        uint32_t chroma = rom->data[GAME_PALETTE_OFS + i * 2];
        uint32_t intensity = rom->data[GAME_PALETTE_OFS + i * 2 + 1];
        uint32_t r = kCryR[chroma] * intensity / 255;
        uint32_t g = kCryG[chroma] * intensity / 255;
        uint32_t b = kCryB[chroma] * intensity / 255;
        pal[i] = (r << 16) | (g << 8) | b;
    }
    return 0;
}

/* ---- colour matching ---------------------------------------------------- */

/* JagToAmiga.BuildRemap: every index but 0 to the nearest colour of `to`
 * (AmigaPalette.GetNearestColor: Euclidean RGB, the first of equals; the
 * squared distance orders as its sqrt does). */
static void BuildRemap(const uint32_t *from, const uint32_t *to, uint8_t map[256])
{
    map[0] = 0;
    for (int32_t i = 1; i < 256; i++) {
        int32_t r = (int32_t)(from[i] >> 16 & 0xFF), g = (int32_t)(from[i] >> 8 & 0xFF), b = (int32_t)(from[i] & 0xFF);
        int32_t best = 0;
        int64_t bestD = -1;
        for (int32_t j = 0; j < 256; j++) {
            int32_t dr = r - (int32_t)(to[j] >> 16 & 0xFF);
            int32_t dg = g - (int32_t)(to[j] >> 8 & 0xFF);
            int32_t db = b - (int32_t)(to[j] & 0xFF);
            int64_t d = (int64_t)dr * dr + (int64_t)dg * dg + (int64_t)db * db;
            if (bestD < 0 || d < bestD) {
                bestD = d;
                best = j;
            }
        }
        map[i] = (uint8_t)best;
    }
}

static void ImagePalette(const SgImage *img, uint32_t pal[256])
{
    for (int32_t i = 0; i < 256; i++)
        pal[i] = ((uint32_t)img->pal[i][0] << 16) | ((uint32_t)img->pal[i][1] << 8) | img->pal[i][2];
}

/* ---- which sprites each Jaguar sheet holds ------------------------------ */

typedef struct { int32_t lo, hi; } Range;

typedef struct {
    const char  *name;
    int32_t      loadOffset;        /* word index of the load opcode in menuColorAndTextScript */
    uint32_t     stream;            /* cartridge address of the packed sheet */
    const char  *teamFile;          /* kit banks: the PC container, slot 227+n = sprite n */
    const Range *only;              /* if set, ONLY these Amiga slots come from the sheet */
    int32_t      numOnly;
    const Range *except;            /* Amiga slots the Jaguar sheet uses for something else */
    int32_t      numExcept;
} JagBankSpec;

/* JagToAmiga.BankTable, found by scoring every placement's pixel mask in the
 * Jaguar sheet against the original Amiga bank. */
static const Range kGrafsOnly[] = {
    /* Only the scoreboard font, the two cards and the "N mins." captions
     * survive; the rest of the sheet is a Jaguar-only alphabet, chevron strip
     * and kit icons on top of the SWOS advertising boards. (137 scores well on
     * shape -- both cells are solid -- but the Jaguar one is a slice of its
     * "HALF-TIME" banner, not the blue block.) */
    { 139, 140 }, { 173, 192 }, { 195, 195 }, { 214, 216 },
};
static const Range kBitsExcept[] = {
    { 1090, 1090 },     /* the Jaguar has a 3-pixel smudge where the Amiga has 13 */
    { 1093, 1093 },     /* blank on the Amiga; unrelated pixels on the Jaguar */
};
static const Range kBenchExcept[] = {
    { 1171, 1173 },     /* blank on the Amiga; the Jaguar parks two advertising posts there */
    { 1188, 1195 },     /* the Jaguar repeats its one (brown) manager over COACH2's cells */
};

/* In JagToAmiga.BankTable's order, which is the order -jag2amiga walks (and
 * prints) them in. Sg_JagBanks leaves CJCGRAFS out: the game takes only the
 * raw sheet from it. */
enum { JAG_CJCGRAFS, JAG_CJCTEAM1, JAG_CJCTEAM2, JAG_CJCTEAM3, JAG_CJCTEAMG, JAG_CJCBITS, JAG_CJCBENCH,
       JAG_ALL_BANKS };

static const JagBankSpec kBanks[JAG_ALL_BANKS] = {
    { "CJCGRAFS",    0, 0x8947C0u, NULL,        kGrafsOnly, 4, NULL, 0 },
    { "CJCTEAM1",  405, 0x8AC828u, "TEAM1.DAT", NULL, 0, NULL, 0 },
    { "CJCTEAM2",  405, 0x8AFB58u, "TEAM2.DAT", NULL, 0, NULL, 0 },
    { "CJCTEAM3",  405, 0x8B2FE0u, "TEAM3.DAT", NULL, 0, NULL, 0 },
    { "CJCTEAMG", 3418, 0x8A02E8u, NULL,        NULL, 0, NULL, 0 },
    { "CJCBITS",  4547, 0x8993D8u, NULL,        NULL, 0, kBitsExcept, 2 },
    { "CJCBENCH", 4889, 0x89D0B0u, NULL,        NULL, 0, kBenchExcept, 2 },
};

#define DOS_GAMEPLAY_OFFSET 114     /* PC index = Amiga index + 114 */
#define TEAM_BLOCK_BASE     227     /* first Amiga slot of the kit block */

static int32_t InRanges(const Range *r, int32_t n, int32_t v)
{
    for (int32_t i = 0; i < n; i++)
        if (v >= r[i].lo && v <= r[i].hi)
            return 1;
    return 0;
}

/* JagBank.UsesJaguar */
static int32_t UsesJaguar(const JagBankSpec *spec, int32_t amigaIndex)
{
    if (spec->only)
        return InRanges(spec->only, spec->numOnly, amigaIndex);
    return !InRanges(spec->except, spec->numExcept, amigaIndex);
}

static const SgDosSprite *SpriteFor(const JagBankSpec *spec, const SgDosSprites *model,
                                    const SgDosDat *team, int32_t amigaIndex)
{
    int32_t i;

    if (spec->teamFile) {
        i = amigaIndex - TEAM_BLOCK_BASE;
        return (team && i >= 0 && i < team->count) ? &team->spr[i] : NULL;
    }
    i = amigaIndex + DOS_GAMEPLAY_OFFSET;
    return (i >= 0 && i < SG_DOS_NUM_SPRITES) ? model->spr[i] : NULL;
}

/* ---- a growing text buffer (the placements .csv) ------------------------- */

typedef struct {
    char    *s;
    uint32_t len, cap;
} Text;

static int32_t TextAdd(Text *t, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);

    if (t->len + n + 1 > t->cap) {
        uint32_t cap = t->cap ? t->cap * 2 : 4096;
        char *p;
        while (cap < t->len + n + 1)
            cap *= 2;
        p = realloc(t->s, cap);
        if (!p)
            return -1;
        t->s = p;
        t->cap = cap;
    }
    memcpy(t->s + t->len, s, n + 1);
    t->len += n;
    return 0;
}

/* ---- one bank's run ------------------------------------------------------ */

/* What a bank build reads and writes beyond the bank itself. Sg_JagBanks uses
 * only celebDir (kit banks only) and the CJCBENCH crops derived in code; the
 * -jag2amiga run adds the patch folder, the template and the log. */
typedef struct {
    const char *celebDir;       /* <bank>_CELEB.bmp; NULL: none */
    int32_t     celebAllBanks;  /* look for _CELEB.bmp for every bank (SwosGfx does), not just the kits */
    const char *patchDir;       /* <bank>_PATCH.bmp, <bank>_PATCH_<x>_<y>.bmp; NULL: none */
    uint8_t    *dosBuf;         /* template: every placement from the PC sprites; NULL: none */
    uint8_t    *todoBuf;        /* template: only the cells not taken from the sheet */
    Text       *csv;            /* template: the placement list */
    SgLogFn     log;
    void       *user;
    /* out */
    int32_t     fromJag, fromDos, empty, celebrated;
} BankRun;

static void Log(const BankRun *r, const char *line)
{
    if (r->log)
        r->log(r->user, line);
}

/* "%-8s" of the bank name, as SwosGfx's {bank.Name,-8}. */
#define NAME8 "%-8s"

/* ---- patches over the sheet --------------------------------------------- */

static int32_t SamePalette(const uint32_t *a, const uint32_t *b)
{
    for (int32_t i = 0; i < 256; i++)
        if ((a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF))
            return 0;
    return 1;
}

/* The celebration block: 3x2 cells of 16x16 at (144,64), blank in every
 * Jaguar kit sheet. Stamped whole; a drawing in another palette is matched to
 * the nearest Jaguar colours (JagToAmiga.ApplyCelebrationPatch). 1 when
 * stamped, 0 when there is no file, -1 on a bad one. */
#define CELEB_X 144
#define CELEB_Y 64
#define CELEB_W 48
#define CELEB_H 32

static int32_t ApplyCelebration(uint8_t *sheet, const BankRun *run, const char *bankName,
                                const uint32_t *jagPal, char *err)
{
    char rel[64], path[1024], line[512];
    SgImage art;
    uint32_t pal[256];
    uint8_t map[256];
    int32_t same;

    if (!run->celebDir || !run->celebDir[0])
        return 0;
    snprintf(rel, sizeof rel, "%s_CELEB.bmp", bankName);
    if (Sg_FindNoCase(run->celebDir, rel, path, sizeof path) != 0 || !Sg_IsFile(path))
        return 0;
    memset(&art, 0, sizeof art);
    if (SgImage_ReadBmp(&art, path, err) != 0)
        return -1;
    if (art.w != CELEB_W || art.h != CELEB_H) {
        snprintf(err, 256, "'%s' is %dx%d, expected %dx%d (the bank block at (%d,%d)).",
                 Sg_PathFileName(path), (int)art.w, (int)art.h, CELEB_W, CELEB_H, CELEB_X, CELEB_Y);
        SgImage_Free(&art);
        return -1;
    }
    ImagePalette(&art, pal);
    same = SamePalette(pal, jagPal);
    if (!same) {
        BuildRemap(pal, jagPal, map);
        snprintf(line, sizeof line, "  " NAME8 " '%s' is not in the Jaguar palette - remapping.",
                 bankName, Sg_PathFileName(path));
        Log(run, line);
    }
    for (int32_t y = 0; y < CELEB_H; y++)
        for (int32_t x = 0; x < CELEB_W; x++) {
            uint8_t v = art.px[y * CELEB_W + x];
            sheet[(CELEB_Y + y) * SG_BANK_W + CELEB_X + x] = same ? v : map[v];
        }
    SgImage_Free(&art);
    return 1;
}

/* The manager crops: the PC game's COACH2 frames (which are the Amiga's) in
 * the Jaguar palette, its shadow colour 8 on the Jaguar's shadow index. They
 * are cut from the bench bank drawn wholly from the PC sprites that way. */
#define DOS_SHADOW_COLOUR 8
#define JAG_SHADOW_INDEX  30

static void CutCrop(const uint8_t *bank, int32_t x0, int32_t y0, int32_t w, int32_t h, uint8_t *crop)
{
    for (int32_t y = 0; y < h; y++)
        memcpy(crop + y * w, bank + (y0 + y) * SG_BANK_W + x0, (size_t)w);
}

/* The SELECT SUBSTITUTE arrow (sprite 1170, 9x8 at (272,120)). The Jaguar draws
 * it pointing left; SWOS puts it at the top of the bench pointing right. Its
 * drawing is mirrored inside the window and its drop shadow -- indices 60-62,
 * one pixel down-right of the art -- is cast again from the mirrored art, each
 * row in the shade the Jaguar's own shadow has in that row (62 where it has none). */
#define ARROW_X 272
#define ARROW_Y 120
#define ARROW_W 9
#define ARROW_H 8

static int32_t IsJagArrowShadow(uint8_t v) { return v >= 60 && v <= 62; }

static void MakeArrow(const uint8_t *sheet, uint8_t crop[ARROW_W * ARROW_H])
{
    uint8_t shade[ARROW_H];
    uint8_t art[ARROW_W * ARROW_H];

    memset(crop, 0, ARROW_W * ARROW_H);
    memset(art, 0, sizeof art);
    for (int32_t y = 0; y < ARROW_H; y++) {
        shade[y] = 62;
        for (int32_t x = 0; x < ARROW_W; x++) {
            uint8_t v = sheet[(ARROW_Y + y) * SG_BANK_W + ARROW_X + x];
            if (IsJagArrowShadow(v)) {
                if (shade[y] == 62)
                    shade[y] = v;
            } else if (v) {
                crop[y * ARROW_W + (ARROW_W - 1 - x)] = v;
                art[y * ARROW_W + (ARROW_W - 1 - x)] = 1;
            }
        }
    }
    for (int32_t y = 0; y + 1 < ARROW_H; y++)
        for (int32_t x = 0; x + 1 < ARROW_W; x++)
            if (art[y * ARROW_W + x] && !art[(y + 1) * ARROW_W + x + 1])
                crop[(y + 1) * ARROW_W + x + 1] = shade[y + 1];
}

/* SwosGfx shipped the three CJCBENCH crops as files; they are derived here, under
 * the names SwosGfx read them by, so they sort among (and can be overridden by)
 * the patch folder's own crops exactly as those files did. */
typedef enum { DERIVED_NONE, DERIVED_COACH_SIT, DERIVED_COACH_STAND, DERIVED_ARROW } Derived;

static const struct { const char *name; Derived kind; int32_t x, y, w, h; } kDerived[] = {
    { "CJCBENCH_PATCH_16_24.bmp",   DERIVED_COACH_SIT,    16, 24, 48, 24 },  /* sitting trio, 1189-1191 */
    { "CJCBENCH_PATCH_256_24.bmp",  DERIVED_COACH_STAND, 256, 24, 48, 24 },  /* standing trio, 1192-1194 */
    { "CJCBENCH_PATCH_272_120.bmp", DERIVED_ARROW,       ARROW_X, ARROW_Y, ARROW_W, ARROW_H },
};
#define NUM_DERIVED ((int32_t)(sizeof kDerived / sizeof kDerived[0]))

typedef struct {
    char    name[260];
    char    path[1024];     /* a file in the patch folder; "" for a derived crop */
    Derived kind;
    int32_t x, y;
} PatchPart;

static int32_t ComparePart(const void *a, const void *b)
{
    return Sg_CompareNoCase(((const PatchPart *)a)->name, ((const PatchPart *)b)->name);
}

static int ComparePartQ(const void *a, const void *b) { return (int)ComparePart(a, b); }

/* int.TryParse: optional blanks, an optional sign, digits, optional blanks. */
static int32_t ParseInt(const char *s, size_t n, int32_t *out)
{
    size_t i = 0;
    int64_t v = 0;
    int32_t neg = 0, digits = 0;

    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (i < n && (s[i] == '+' || s[i] == '-'))
        neg = s[i++] == '-';
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i++] - '0');
        digits++;
        if (v > 2147483648LL)
            return 0;
    }
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (!digits || i != n || (!neg && v > 2147483647LL))
        return 0;
    *out = (int32_t)(neg ? -v : v);
    return 1;
}

/* Hand-drawn art laid over the unpacked sheet before the placement walk
 * (JagToAmiga.ApplyBankPatch): <bank>_PATCH.bmp, a whole 320x256 bank whose
 * colour 0 is transparent, then the <bank>_PATCH_<x>_<y>.bmp crops in name
 * order, each REPLACING its rectangle, zeros included. A crop is the art at
 * that spot now, and the sprite it covers is usually another shape, so
 * anything left showing through would be the old one's silhouette. Every
 * pixel a part touches is marked in `mask`, and the walk takes the cells the
 * mask covers from the sheet whatever the bank table says. *patched is set
 * when anything was painted (SwosGfx then returns its mask, else null). */
static int32_t ApplyBankPatch(uint8_t *sheet, uint8_t *mask, int32_t *patched, const JagBankSpec *spec,
                              const BankRun *run, const uint8_t *benchTmpl,
                              const uint32_t *jagPal, char *err)
{
    PatchPart *parts = NULL;
    int32_t nparts = 0, ncap = 0, rc = -1, painted = 0, haveWhole = 0;
    char wholeRel[64], prefix[64], line[600];
    char **names = NULL;
    int32_t nnames = 0;

    *patched = 0;
    snprintf(wholeRel, sizeof wholeRel, "%s_PATCH.bmp", spec->name);
    snprintf(prefix, sizeof prefix, "%s_PATCH_", spec->name);

#define ADD_PART(nm, pth, knd, px_, py_)                                         \
    do {                                                                         \
        if (nparts == ncap) {                                                    \
            int32_t nc_ = ncap ? ncap * 2 : 8;                                   \
            PatchPart *np_ = realloc(parts, (size_t)nc_ * sizeof *np_);          \
            if (!np_) { snprintf(err, 256, "out of memory"); goto out; }         \
            parts = np_;                                                         \
            ncap = nc_;                                                          \
        }                                                                        \
        snprintf(parts[nparts].name, sizeof parts[nparts].name, "%s", (nm));     \
        snprintf(parts[nparts].path, sizeof parts[nparts].path, "%s", (pth));    \
        parts[nparts].kind = (knd);                                              \
        parts[nparts].x = (px_);                                                 \
        parts[nparts].y = (py_);                                                 \
        nparts++;                                                                \
    } while (0)

    if (run->patchDir && run->patchDir[0] && Sg_IsDir(run->patchDir)) {
        char path[1024];
        if (Sg_FindNoCase(run->patchDir, wholeRel, path, sizeof path) == 0 && Sg_IsFile(path)) {
            ADD_PART(Sg_PathFileName(path), path, DERIVED_NONE, 0, 0);
            haveWhole = 1;
        }
        if (Sg_ListDir(run->patchDir, &names, &nnames) != 0) {
            snprintf(err, 256, "cannot read the folder %.200s", run->patchDir);
            goto out;
        }
        for (int32_t i = 0; i < nnames; i++) {
            const char *nm = names[i];
            size_t len = strlen(nm), pl = strlen(prefix);
            const char *tail, *us;
            int32_t px, py;

            if (!Sg_StartsWithNoCase(nm, prefix) || !Sg_EndsWithNoCase(nm, ".bmp") || len < pl + 4)
                continue;
            /* <x>_<y>: the name without ".bmp", after the prefix, split on '_' into two numbers */
            tail = nm + pl;
            us = memchr(tail, '_', len - 4 - pl);
            if (!us || memchr(us + 1, '_', (size_t)(nm + len - 4 - (us + 1))) ||
                !ParseInt(tail, (size_t)(us - tail), &px) ||
                !ParseInt(us + 1, (size_t)(nm + len - 4 - (us + 1)), &py)) {
                snprintf(err, 256, "'%s': a positioned patch is named %s<x>_<y>.bmp, e.g. %s16_24.bmp.",
                         nm, prefix, prefix);
                goto out;
            }
            Sg_PathJoin(path, sizeof path, run->patchDir, nm);
            ADD_PART(nm, path, DERIVED_NONE, px, py);
        }
    }
    /* The CJCBENCH crops SwosGfx shipped as files, unless the folder has its own. */
    if (benchTmpl)
        for (int32_t d = 0; d < NUM_DERIVED; d++) {
            int32_t have = 0;
            for (int32_t i = haveWhole; i < nparts && !have; i++)
                have = Sg_CompareNoCase(parts[i].name, kDerived[d].name) == 0;
            if (!have)
                ADD_PART(kDerived[d].name, "", kDerived[d].kind, kDerived[d].x, kDerived[d].y);
        }
#undef ADD_PART

    if (nparts - haveWhole > 1)
        qsort(parts + haveWhole, (size_t)(nparts - haveWhole), sizeof *parts, ComparePartQ);

    for (int32_t k = 0; k < nparts; k++) {
        const PatchPart *pp = &parts[k];
        int32_t isWhole = haveWhole && k == 0;
        int32_t replace = !isWhole, here = 0, w, h, same = 1;
        uint8_t crop[48 * 24], map[256];
        const uint8_t *px;
        SgImage art;
        uint32_t pal[256];

        memset(&art, 0, sizeof art);
        if (pp->kind == DERIVED_NONE) {
            if (SgImage_ReadBmp(&art, pp->path, err) != 0)
                goto out;
            w = art.w;
            h = art.h;
            px = art.px;
            ImagePalette(&art, pal);
            same = SamePalette(pal, jagPal);
        } else {
            w = kDerived[pp->kind - 1].w;
            h = kDerived[pp->kind - 1].h;
            if (pp->kind == DERIVED_ARROW)
                MakeArrow(sheet, crop);
            else
                CutCrop(benchTmpl, pp->x, pp->y, w, h, crop);
            px = crop;
        }

        if (pp->x == 0 && pp->y == 0 && nparts == 1 && (w != SG_BANK_W || h != SG_BANK_H) && isWhole) {
            snprintf(err, 256, "'%.120s' is %dx%d, expected a whole %dx%d bank. A crop is named %.40s<x>_<y>.bmp instead.",
                     pp->name, (int)w, (int)h, SG_BANK_W, SG_BANK_H, prefix);
            SgImage_Free(&art);
            goto out;
        }
        if (pp->x < 0 || pp->y < 0 || pp->x + w > SG_BANK_W || pp->y + h > SG_BANK_H) {
            snprintf(err, 256, "'%.120s' is %dx%d at (%d,%d), which runs outside the %dx%d bank.",
                     pp->name, (int)w, (int)h, (int)pp->x, (int)pp->y, SG_BANK_W, SG_BANK_H);
            SgImage_Free(&art);
            goto out;
        }
        if (!same) {
            BuildRemap(pal, jagPal, map);
            snprintf(line, sizeof line, "  " NAME8 " '%s' is not in the Jaguar palette - remapping.",
                     spec->name, pp->name);
            Log(run, line);
        }
        for (int32_t y = 0; y < h; y++)
            for (int32_t x = 0; x < w; x++) {
                uint8_t v = px[y * w + x];
                int32_t o = (pp->y + y) * SG_BANK_W + pp->x + x;
                if (v == 0 && !replace)
                    continue;
                sheet[o] = v != 0 && !same ? map[v] : v;
                mask[o] = 1;
                if (v)
                    here++;
            }
        SgImage_Free(&art);
        if (replace)
            snprintf(line, sizeof line, "  " NAME8 " '%s' replaced %dx%d at (%d,%d), %d pixels drawn.",
                     spec->name, pp->name, (int)w, (int)h, (int)pp->x, (int)pp->y, (int)here);
        else
            snprintf(line, sizeof line, "  " NAME8 " '%s' painted %d pixels.", spec->name, pp->name, (int)here);
        Log(run, line);
        painted += replace ? w * h : here;
    }
    *patched = painted > 0;
    rc = 0;
out:
    Sg_ListDirFree(names, nnames);
    free(parts);
    return rc;
}

/* ---- the walk ----------------------------------------------------------- */

/* Copy a window out of the sheet, 0 transparent. 1 when it drew anything. */
static int32_t CopyFromSheet(uint8_t *dst, const uint8_t *sheet, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t written = 0;

    for (int32_t y = 0; y < h; y++) {
        int32_t by = y0 + y;
        if (by < 0 || by >= SG_BANK_H)
            continue;
        for (int32_t x = 0; x < w; x++) {
            int32_t bx = x0 + x;
            uint8_t v;
            if (bx < 0 || bx >= SG_BANK_W)
                continue;
            v = sheet[by * SG_BANK_W + bx];
            if (!v)
                continue;
            dst[by * SG_BANK_W + bx] = v;
            written++;
        }
    }
    return written > 0;
}

static int32_t MaskCovers(const uint8_t *mask, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    if (!mask)
        return 0;
    for (int32_t y = y0 < 0 ? 0 : y0; y < y0 + h && y < SG_BANK_H; y++)
        for (int32_t x = x0 < 0 ? 0 : x0; x < x0 + w && x < SG_BANK_W; x++)
            if (mask[y * SG_BANK_W + x])
                return 1;
    return 0;
}

/* A decoded PC sprite: its own indices, 0..15, 0 transparent. */
typedef struct {
    uint8_t *px;
    int32_t  w, h;
} Decoded;

static int32_t Decode(const SgDosSprite *spr, Decoded *d, char *err)
{
    d->px = NULL;
    d->w = d->h = 0;
    if (!spr || spr->width <= 0 || spr->height <= 0)
        return 0;
    d->px = malloc((size_t)spr->width * (size_t)spr->height);
    if (!d->px) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    if (Sg_DosSpriteDecode(spr, d->px) != 0) {
        snprintf(err, 256, "a PC sprite's record does not fit its data");
        free(d->px);
        d->px = NULL;
        return -1;
    }
    d->w = spr->width;
    d->h = spr->height;
    return 0;
}

/* JagToAmiga.BlitRemapped: transparency is decided on the PC index, so a colour
 * whose nearest Jaguar match is index 0 still writes its 0. */
static void BlitRemapped(uint8_t *bank, const Decoded *d, const SgPlacement *p, const uint8_t map[256])
{
    int32_t limW = d->w < p->winW ? d->w : p->winW;
    int32_t limH = d->h < p->winH ? d->h : p->winH;

    for (int32_t y = 0; y < limH; y++) {
        int32_t by = p->y0 + y;
        if (by < 0 || by >= SG_BANK_H)
            continue;
        for (int32_t x = 0; x < limW; x++) {
            uint8_t v = d->px[y * d->w + x];
            int32_t bx = p->x0 + x;
            if (v == 0 || bx < 0 || bx >= SG_BANK_W)
                continue;
            bank[by * SG_BANK_W + bx] = map[v];
        }
    }
}

/* The bench bank drawn wholly from the PC sprites with `map` (the template
 * the manager crops are cut from). */
static int32_t RenderDosTemplate(const JagBankSpec *spec, const SgPlacementList *places,
                                 const SgDosSprites *model, const uint8_t map[256],
                                 uint8_t *out, char *err)
{
    memset(out, 0, SHEET_SIZE);
    for (int32_t k = 0; k < places->count; k++) {
        Decoded d;
        if (Decode(SpriteFor(spec, model, NULL, places->items[k].amigaIndex), &d, err) != 0)
            return -1;
        if (d.px)
            BlitRemapped(out, &d, &places->items[k], map);
        free(d.px);
    }
    return 0;
}

static int32_t BuildBank(const JagBankSpec *spec, const SgBuf *rom, const SgPlacementList *places,
                         const SgDosSprites *model, const char *dosDir,
                         const uint32_t *jagPal, const uint8_t *dosToJag,
                         SgImage *bank, BankRun *run, char *err)
{
    SgDosDat team;
    SgBuf sheet;
    uint8_t *mask = NULL, *tmpl = NULL;
    int32_t rc = -1, patched = 0, isBench = strcmp(spec->name, "CJCBENCH") == 0;

    memset(&team, 0, sizeof team);
    sheet.data = NULL;
    run->fromJag = run->fromDos = run->empty = run->celebrated = 0;
    if (SgImage_Init(bank, SG_BANK_W, SG_BANK_H) != 0) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    SgImage_SetPaletteArgb(bank, jagPal, 256);
    if (Sg_JagUnpack(rom, spec->stream, SHEET_SIZE, &sheet, err) != 0)
        goto out;
    if (spec->teamFile && Sg_DosDatLoad(&team, dosDir, spec->teamFile, SG_DOS_TEAM_SPRITES, err) != 0)
        goto out;

    /* Fill in the celebration cells the Jaguar release never had, so the walk
     * reads them like any other Jaguar sprite. */
    if (spec->teamFile || run->celebAllBanks) {
        int32_t c = ApplyCelebration(sheet.data, run, spec->name, jagPal, err);
        if (c < 0)
            goto out;
        run->celebrated = c;
    }
    mask = calloc(1, SHEET_SIZE);
    if (!mask) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    if (isBench) {
        uint8_t shadowMap[256];
        tmpl = malloc(SHEET_SIZE);
        if (!tmpl) {
            snprintf(err, 256, "out of memory");
            goto out;
        }
        memcpy(shadowMap, dosToJag, 256);
        shadowMap[DOS_SHADOW_COLOUR] = JAG_SHADOW_INDEX;
        if (RenderDosTemplate(spec, places, model, shadowMap, tmpl, err) != 0)
            goto out;
    }
    if (ApplyBankPatch(sheet.data, mask, &patched, spec, run, tmpl, jagPal, err) != 0)
        goto out;

    for (int32_t k = 0; k < places->count; k++) {
        const SgPlacement *p = &places->items[k];
        Decoded d;
        int32_t w, h, took;

        if (Decode(SpriteFor(spec, model, &team, p->amigaIndex), &d, err) != 0)
            goto out;
        /* The sprite table is shared across releases, so the PC sprite's size is
         * the window lifted from the sheet; without one, the script's window. */
        w = d.px && d.w < p->winW ? d.w : p->winW;
        h = d.px && d.h < p->winH ? d.h : p->winH;
        /* A cell a patch paints is taken from the sheet whatever the bank table
         * says -- that is the whole point of painting it. */
        took = (UsesJaguar(spec, p->amigaIndex) || (patched && MaskCovers(mask, p->x0, p->y0, w, h))) &&
               CopyFromSheet(bank->px, sheet.data, p->x0, p->y0, w, h);
        if (took)
            run->fromJag++;
        /* The DOS template is every placement (the whole original bank in the
         * Jaguar palette); the TODO sheet only the cells the sheet did not supply. */
        if (d.px && run->dosBuf)
            BlitRemapped(run->dosBuf, &d, p, dosToJag);
        if (!took) {
            if (!d.px)
                run->empty++;
            else {
                BlitRemapped(bank->px, &d, p, dosToJag);
                if (run->todoBuf)
                    BlitRemapped(run->todoBuf, &d, p, dosToJag);
                run->fromDos++;
            }
        }
        if (run->csv) {
            char row[96];
            snprintf(row, sizeof row, "%d,%s,%d,%d,%d,%d\n", (int)p->amigaIndex,
                     took ? "jag" : !d.px ? "blank" : "dos", (int)p->x0, (int)p->y0, (int)w, (int)h);
            if (TextAdd(run->csv, row) != 0) {
                free(d.px);
                snprintf(err, 256, "out of memory");
                goto out;
            }
        }
        free(d.px);
    }
    rc = 0;
out:
    free(mask);
    free(tmpl);
    Sg_BufFree(&sheet);
    Sg_DosDatFree(&team);
    if (rc != 0)
        SgImage_Free(bank);
    return rc;
}

int32_t Sg_JagBanks(const SgBuf *rom, const char *dosDir, const char *celebDir,
                    SgImage banks[SG_JAG_BANK_COUNT], SgImage *grafsSheet,
                    uint32_t *celebrations, char *err)
{
    SgScriptPlacements script;
    SgDosSprites *model = NULL;
    uint32_t jagPal[256];
    uint8_t dosToJag[256];
    const uint16_t *words;
    int32_t n, done = 0, rc = -1;
    SgBuf sheet;

    memset(&script, 0, sizeof script);
    for (int32_t b = 0; b < SG_JAG_BANK_COUNT; b++)
        memset(&banks[b], 0, sizeof banks[b]);
    if (grafsSheet)
        memset(grafsSheet, 0, sizeof *grafsSheet);
    if (celebrations)
        *celebrations = 0;

    if (Sg_JagGamePalette(rom, jagPal, err) != 0)
        return -1;
    BuildRemap(Sg_DosSpritePalGame, jagPal, dosToJag);
    words = Sg_ScriptWords(SG_SCRIPT_MENU_COLOR_TEXT, &n);
    if (Sg_ScriptInterpret(words, n, &script, err) != 0)
        return -1;
    model = calloc(1, sizeof *model);
    if (!model) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    if (Sg_DosSpritesLoad(model, dosDir, err) != 0)
        goto out;

    /* SgJagBank n is kBanks[n + 1]: the same banks, CJCGRAFS left out. */
    for (done = 0; done < SG_JAG_BANK_COUNT; done++) {
        const JagBankSpec *spec = &kBanks[done + 1];
        const SgPlacementList *places = Sg_ScriptFindLoad(&script, spec->loadOffset);
        BankRun run;

        memset(&run, 0, sizeof run);
        run.celebDir = celebDir;
        if (!places) {
            snprintf(err, 256, "%s: no bank load at word %d of its script", spec->name, (int)spec->loadOffset);
            goto out;
        }
        if (BuildBank(spec, rom, places, model, dosDir, jagPal, dosToJag, &banks[done], &run, err) != 0)
            goto out;
        if (run.celebrated && celebrations)
            *celebrations |= 1u << done;
    }

    if (grafsSheet) {
        if (Sg_JagUnpack(rom, kBanks[JAG_CJCGRAFS].stream, SHEET_SIZE, &sheet, err) != 0)
            goto out;
        if (SgImage_Init(grafsSheet, SG_BANK_W, SG_BANK_H) != 0) {
            Sg_BufFree(&sheet);
            snprintf(err, 256, "out of memory");
            goto out;
        }
        memcpy(grafsSheet->px, sheet.data, SHEET_SIZE);
        SgImage_SetPaletteArgb(grafsSheet, jagPal, 256);
        Sg_BufFree(&sheet);
    }
    rc = 0;
out:
    if (rc != 0)
        for (int32_t b = 0; b < done && b < SG_JAG_BANK_COUNT; b++)
            SgImage_Free(&banks[b]);
    if (model)
        Sg_DosSpritesFree(model);
    free(model);
    Sg_ScriptPlacementsFree(&script);
    return rc;
}

/* ---- -jag2amiga ---------------------------------------------------------- */

int32_t Sg_JagFindRom(const char *jagDir, char *out, uint32_t cap)
{
    static const char *const kExt[] = { ".jag", ".j64", ".rom", ".bin" };
    char **names = NULL;
    int32_t n = 0, rc = -1;

    if (!jagDir || !Sg_IsDir(jagDir) || Sg_ListDir(jagDir, &names, &n) != 0)
        return -1;
    /* Each extension in turn, the first name of it in OrdinalIgnoreCase order
     * (the listing's order). */
    for (int32_t e = 0; e < 4 && rc != 0; e++)
        for (int32_t i = 0; i < n; i++)
            if (Sg_EndsWithNoCase(names[i], kExt[e])) {
                Sg_PathJoin(out, cap, jagDir, names[i]);
                rc = 0;
                break;
            }
    Sg_ListDirFree(names, n);
    return rc;
}

static int32_t WriteSheet(const char *outDir, const char *name, const char *suffix,
                          const uint8_t *px, const uint32_t *pal, char *path, uint32_t cap, char *err)
{
    SgImage img;
    char file[64];
    int32_t rc;

    if (SgImage_Init(&img, SG_BANK_W, SG_BANK_H) != 0) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    memcpy(img.px, px, SHEET_SIZE);
    SgImage_SetPaletteArgb(&img, pal, 256);
    snprintf(file, sizeof file, "%s%s.bmp", name, suffix);
    Sg_PathJoin(path, cap, outDir, file);
    rc = SgImage_WriteBmp(&img, path, err);
    SgImage_Free(&img);
    return rc;
}

int32_t Sg_Jag2AmigaRun(const char *dosDir, const char *romPath, const char *jagDir, const char *outDir,
                        int32_t makeTemplate, int32_t dumpSheets, SgLogFn log, void *user, char *err)
{
    SgScriptPlacements script;
    SgDosSprites *model = NULL;
    SgImage dosBanks[SG_BANK_COUNT];
    uint32_t jagPal[256];
    uint8_t dosToJag[256], act[768];
    uint8_t *dosBuf = NULL, *todoBuf = NULL;
    const uint16_t *words;
    int32_t n, rc = -1, banks = 0;
    int32_t wantSheets = dumpSheets || getenv("SWOSGFX_DUMP_JAG_SHEETS") != NULL;
    int32_t wantTemplate = makeTemplate || getenv("SWOSGFX_JAG_TEMPLATE") != NULL;
    static const SgBank kDosOnly[] = { SG_BANK_SOCCER_S, SG_BANK_CHARSET, SG_BANK_MENUS, SG_BANK_MENUS2 };
    char path[1024], line[1400];
    SgBuf rom;
    Text csv;

    memset(&script, 0, sizeof script);
    memset(dosBanks, 0, sizeof dosBanks);
    memset(&csv, 0, sizeof csv);
    rom.data = NULL;
    if (!outDir || !outDir[0])
        outDir = ".";
    if (!jagDir)
        jagDir = "";
    if (Sg_MakeDirs(outDir) != 0) {
        snprintf(err, 256, "cannot create the folder %.200s", outDir);
        return -1;
    }
    if (!Sg_IsFile(romPath)) {
        snprintf(err, 256, "Jaguar ROM not found: %.200s", romPath);
        return -1;
    }
    if (Sg_ReadFile(romPath, &rom, err) != 0)
        return -1;
    if (Sg_JagGamePalette(&rom, jagPal, err) != 0)
        goto out;
    words = Sg_ScriptWords(SG_SCRIPT_MENU_COLOR_TEXT, &n);
    if (Sg_ScriptInterpret(words, n, &script, err) != 0)
        goto out;
    model = calloc(1, sizeof *model);
    if (!model) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    if (Sg_DosSpritesLoad(model, dosDir, err) != 0)
        goto out;
    /* PC fallback sprites arrive in the PC game palette; fold them onto the
     * nearest Jaguar colours so each bank stays one 256-colour picture. */
    BuildRemap(Sg_DosSpritePalGame, jagPal, dosToJag);
    dosBuf = malloc(SHEET_SIZE);
    todoBuf = malloc(SHEET_SIZE);
    if (!dosBuf || !todoBuf) {
        snprintf(err, 256, "out of memory");
        goto out;
    }

    for (int32_t b = 0; b < JAG_ALL_BANKS; b++) {
        const JagBankSpec *spec = &kBanks[b];
        const SgPlacementList *places = Sg_ScriptFindLoad(&script, spec->loadOffset);
        SgImage bank;
        BankRun run;

        if (!places) {
            snprintf(err, 256, "%s: no bank load at word %d of its script", spec->name, (int)spec->loadOffset);
            goto out;
        }
        memset(&run, 0, sizeof run);
        run.celebDir = jagDir;
        run.celebAllBanks = 1;
        run.patchDir = jagDir;
        run.log = log;
        run.user = user;
        if (wantTemplate) {
            memset(dosBuf, 0, SHEET_SIZE);
            memset(todoBuf, 0, SHEET_SIZE);
            csv.len = 0;
            if (TextAdd(&csv, "index,source,x,y,w,h\n") != 0) {
                snprintf(err, 256, "out of memory");
                goto out;
            }
            run.dosBuf = dosBuf;
            run.todoBuf = todoBuf;
            run.csv = &csv;
        }
        /* -jag-dump-sheets: the sheet as unpacked, before any patch or walk --
         * the way to tell "the Jaguar has not got it" from "has it elsewhere".
         * SwosGfx writes it before the patch lines print, so it is unpacked
         * and written here first (BuildBank unpacks it again; it is cheap). */
        if (wantSheets) {
            SgBuf sheet;
            if (Sg_JagUnpack(&rom, spec->stream, SHEET_SIZE, &sheet, err) != 0)
                goto out;
            rc = WriteSheet(outDir, spec->name, "_JAGSHEET", sheet.data, jagPal, path, sizeof path, err);
            Sg_BufFree(&sheet);
            if (rc != 0) {
                rc = -1;
                goto out;
            }
            rc = -1;
            snprintf(line, sizeof line, "  raw Jaguar sheet -> %s", path);
            if (log)
                log(user, line);
        }
        if (BuildBank(spec, &rom, places, model, dosDir, jagPal, dosToJag, &bank, &run, err) != 0)
            goto out;
        Sg_PathJoin(path, sizeof path, outDir, spec->name);
        snprintf(path + strlen(path), sizeof path - strlen(path), ".bmp");
        if (SgImage_WriteBmp(&bank, path, err) != 0) {
            SgImage_Free(&bank);
            goto out;
        }
        SgImage_Free(&bank);
        if (wantTemplate) {
            char file[64];
            if (WriteSheet(outDir, spec->name, "_DOS", dosBuf, jagPal, path, sizeof path, err) != 0 ||
                WriteSheet(outDir, spec->name, "_TODO", todoBuf, jagPal, path, sizeof path, err) != 0)
                goto out;
            snprintf(file, sizeof file, "%s_placements.csv", spec->name);
            Sg_PathJoin(path, sizeof path, outDir, file);
            if (Sg_WriteFile(path, csv.s, csv.len, err) != 0)
                goto out;
        }
        banks++;
        snprintf(line, sizeof line, "  " NAME8 " %3d Jaguar / %3d DOS", spec->name, (int)run.fromJag, (int)run.fromDos);
        if (run.empty > 0)
            snprintf(line + strlen(line), sizeof line - strlen(line), " / %d blank", (int)run.empty);
        if (run.celebrated)
            snprintf(line + strlen(line), sizeof line - strlen(line), "   (+ celebration frames)");
        if (log)
            log(user, line);
    }

    /* The banks the Jaguar has no counterpart for, straight from the PC game as
     * -dos2amiga makes them, so the folder is a complete set. */
    {
        uint32_t mask = 0;
        for (int32_t i = 0; i < 4; i++)
            mask |= 1u << kDosOnly[i];
        if (Sg_DosToAmigaBanksSel(dosDir, mask, dosBanks, err) != 0)
            goto out;
        for (int32_t i = 0; i < 4; i++) {
            char file[64];
            snprintf(file, sizeof file, "%s.bmp", Sg_BankNames[kDosOnly[i]]);
            Sg_PathJoin(path, sizeof path, outDir, file);
            if (SgImage_WriteBmp(&dosBanks[kDosOnly[i]], path, err) != 0)
                goto out;
            banks++;
        }
    }

    /* The Jaguar CLUT beside the banks, which are drawn in it: a plain 768-byte
     * .act (R, G, B x 256, no count trailer -- PalFile.WriteActFile for a full
     * palette with no transparent index). */
    for (int32_t i = 0; i < 256; i++) {
        act[i * 3 + 0] = (uint8_t)(jagPal[i] >> 16);
        act[i * 3 + 1] = (uint8_t)(jagPal[i] >> 8);
        act[i * 3 + 2] = (uint8_t)jagPal[i];
    }
    Sg_PathJoin(path, sizeof path, outDir, "JAG_GAME.act");
    if (Sg_WriteFile(path, act, sizeof act, err) != 0)
        goto out;

    snprintf(line, sizeof line, "Jaguar -> Amiga: wrote %d banks to %s", (int)banks, outDir);
    if (log)
        log(user, line);
    rc = 0;
out:
    for (int32_t b = 0; b < SG_BANK_COUNT; b++)
        SgImage_Free(&dosBanks[b]);
    free(csv.s);
    free(dosBuf);
    free(todoBuf);
    if (model)
        Sg_DosSpritesFree(model);
    free(model);
    Sg_ScriptPlacementsFree(&script);
    Sg_BufFree(&rom);
    return rc;
}
