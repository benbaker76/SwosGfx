/*
 * dos_sprites_test.c -- sprNNNN.bmp export and import of the PC game's sprites.
 *
 *   dos_sprites_test [dosDir]
 *
 * Without the PC game's folder there is nothing to test and it skips. With
 * it, it works on a COPY of SPRITE.DAT and the six containers, made in
 * ./dos_sprites_test.tmp (the game's own files are only read):
 *
 *  1. export -> import of the untouched BMPs. The containers come back the
 *     same size, every record's header byte for byte, and every sprite's
 *     pixels the same; what may differ is only what a BMP cannot carry, the
 *     bits past a sprite's width, which the import zeroes (in the PC game's
 *     files 25 records have some set, and SwosGfx's import zeroes them too).
 *  2. that result is a fixed point: export -> import again leaves every file
 *     byte-identical, and the second export's BMPs equal the first's.
 *  3. malformed input fails with a message and writes nothing: a BMP cut
 *     short, of 4 bits, top-down, too wide, not a BMP; no BMPs at all; a
 *     container missing; SPRITE.DAT the wrong size.
 */
#include "dos_sprite_io.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORK "dos_sprites_test.tmp"

static const char *const kFiles[] = {
    "SPRITE.DAT", "CHARSET.DAT", "SCORE.DAT", "TEAM1.DAT", "TEAM3.DAT", "GOAL1.DAT", "BENCH.DAT",
};
#define NUM_FILES ((int32_t)(sizeof kFiles / sizeof kFiles[0]))

static int32_t g_fail;

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_fail++; } } while (0)

static int32_t TestCopyFile(const char *from, const char *to)
{
    char err[256];
    SgBuf b;
    if (Sg_ReadFile(from, &b, err) != 0) {
        printf("%s\n", err);
        return -1;
    }
    int32_t rc = Sg_WriteFile(to, b.data, b.size, err);
    if (rc)
        printf("%s\n", err);
    Sg_BufFree(&b);
    return rc;
}

/* The game's seven files into `dir`. */
static int32_t CopyGame(const char *dosDir, const char *dir)
{
    char from[1024], to[1024];
    for (int32_t i = 0; i < NUM_FILES; i++) {
        if (Sg_FindNoCase(dosDir, kFiles[i], from, sizeof from) != 0) {
            printf("%s not found in %s\n", kFiles[i], dosDir);
            return -1;
        }
        Sg_PathJoin(to, sizeof to, dir, kFiles[i]);
        if (TestCopyFile(from, to) != 0)
            return -1;
    }
    return 0;
}

static int32_t SameFile(const char *a, const char *b)
{
    char err[256];
    SgBuf x, y;
    int32_t same;
    if (Sg_ReadFile(a, &x, err) != 0)
        return 0;
    if (Sg_ReadFile(b, &y, err) != 0) {
        Sg_BufFree(&x);
        return 0;
    }
    same = x.size == y.size && (x.size == 0 || memcmp(x.data, y.data, x.size) == 0);
    Sg_BufFree(&x);
    Sg_BufFree(&y);
    return same;
}

/* Every file of the game in dirA equals dirB's. */
static int32_t SameGame(const char *a, const char *b)
{
    char pa[1024], pb[1024];
    for (int32_t i = 0; i < NUM_FILES; i++) {
        Sg_PathJoin(pa, sizeof pa, a, kFiles[i]);
        Sg_PathJoin(pb, sizeof pb, b, kFiles[i]);
        if (!SameFile(pa, pb))
            return 0;
    }
    return 1;
}

static int32_t SameSprites(const char *a, const char *b)
{
    char pa[1024], pb[1024], name[16];
    int32_t differ = 0;
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++) {
        snprintf(name, sizeof name, "spr%04d.bmp", (int)i);
        Sg_PathJoin(pa, sizeof pa, a, name);
        Sg_PathJoin(pb, sizeof pb, b, name);
        FILE *fa = fopen(pa, "rb"), *fb = fopen(pb, "rb");
        if (!fa != !fb || (fa && !SameFile(pa, pb)))
            differ++;
        if (fa) fclose(fa);
        if (fb) fclose(fb);
    }
    return differ;
}

static void RemoveSprites(const char *dir)
{
    char p[1024], name[16];
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++) {
        snprintf(name, sizeof name, "spr%04d.bmp", (int)i);
        Sg_PathJoin(p, sizeof p, dir, name);
        remove(p);
    }
}

static void RemoveGame(const char *dir)
{
    char p[1024];
    for (int32_t i = 0; i < NUM_FILES; i++) {
        Sg_PathJoin(p, sizeof p, dir, kFiles[i]);
        remove(p);
    }
}

/* 1: the import of the untouched export, against the original. */
static void CheckRoundTrip(const char *orig, const char *rt)
{
    char err[256];
    SgDosSprites a, b;
    int32_t padOnly = 0;

    if (Sg_DosSpritesLoad(&a, orig, err) != 0 || Sg_DosSpritesLoad(&b, rt, err) != 0) {
        CHECK(0, "%s", err);
        return;
    }
    for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++)
        CHECK(a.dat[di].buf.size == b.dat[di].buf.size, "%s: %u bytes, was %u", Sg_DosDatSpecs[di].name,
              (unsigned)b.dat[di].buf.size, (unsigned)a.dat[di].buf.size);
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++) {
        const SgDosSprite *x = a.spr[i], *y = b.spr[i];
        /* The header sits right before the pixels. */
        CHECK(memcmp(x->data - 24, y->data - 24, 24) == 0, "sprite %d: the header changed", (int)i);
        CHECK(x->size == y->size, "sprite %d: %u bytes, was %u", (int)i, (unsigned)y->size, (unsigned)x->size);
        if (x->size != y->size || x->width <= 0 || x->height <= 0)
            continue;
        uint8_t *px = malloc((size_t)x->width * x->height), *py = malloc((size_t)x->width * x->height);
        CHECK(Sg_DosSpriteDecode(x, px) == 0 && Sg_DosSpriteDecode(y, py) == 0, "sprite %d: decode", (int)i);
        CHECK(memcmp(px, py, (size_t)x->width * x->height) == 0, "sprite %d: the pixels changed", (int)i);
        if (memcmp(x->data, y->data, x->size) != 0)
            padOnly++;
        free(px);
        free(py);
    }
    printf("round trip: %d records differ only past their width\n", (int)padOnly);
    Sg_DosSpritesFree(&a);
    Sg_DosSpritesFree(&b);
}

/* A BMP of `w` x `h` at `path`, with header fields to spoil. */
static void WriteBmp(const char *path, int32_t w, int32_t h, uint16_t bpp, uint32_t cut)
{
    char err[256];
    uint32_t row = (((uint32_t)(w < 0 ? -w : w) * bpp + 31) / 32) * 4;
    uint32_t hAbs = (uint32_t)(h < 0 ? -h : h);
    uint32_t size = 54 + 1024 + row * hAbs;
    uint8_t *f = calloc(1, size);
    f[0] = 'B'; f[1] = 'M';
    f[10] = (54 + 1024) & 0xFF; f[11] = (54 + 1024) >> 8;
    f[14] = 40;
    memcpy(f + 18, &w, 4);   /* little-endian hosts only, which the tests run on */
    memcpy(f + 22, &h, 4);
    f[26] = 1;
    f[28] = (uint8_t)bpp;
    for (uint32_t i = 54 + 1024; i < size; i++)
        f[i] = (uint8_t)(i * 7);
    Sg_WriteFile(path, f, size - cut, err);
    free(f);
}

/* Import `bmp` (or an empty folder) into a fresh copy: it must fail, and
 * leave the copy as it was. */
static void ExpectImportFails(const char *dosDir, const char *what, int32_t kind)
{
    char err[256] = "", dir[1024], sprites[1024], p[1024];
    snprintf(dir, sizeof dir, WORK "/bad");
    snprintf(sprites, sizeof sprites, WORK "/badspr");
    RemoveGame(dir);
    RemoveSprites(sprites);
    Sg_PathJoin(p, sizeof p, sprites, "SPR0400.BMP");
    remove(p);
    if (CopyGame(dosDir, dir) != 0) {
        CHECK(0, "copy");
        return;
    }
    Sg_MakeDirs(sprites);
    /* Two good sprites first, so a failure after some were read is tested. */
    Sg_PathJoin(p, sizeof p, WORK "/a", "spr0005.bmp");
    TestCopyFile(p, WORK "/badspr/spr0005.bmp");
    Sg_PathJoin(p, sizeof p, WORK "/a", "spr1300.bmp");
    TestCopyFile(p, WORK "/badspr/spr1300.bmp");
    Sg_PathJoin(p, sizeof p, sprites, "SPR0400.BMP");
    switch (kind) {
    case 0: WriteBmp(p, 16, 10, 8, 30); break;      /* cut short */
    case 1: WriteBmp(p, 16, 10, 4, 0); break;       /* 4 bits */
    case 2: WriteBmp(p, 16, -10, 8, 0); break;      /* top-down */
    case 3: WriteBmp(p, 600, 4, 8, 0); break;       /* too wide */
    case 4: Sg_WriteFile(p, "hello", 5, err); break;
    case 5: WriteBmp(p, -16, 10, 8, 0); break;      /* negative width */
    case 6: WriteBmp(p, 16, 10, 8, 1198); break;    /* header cut */
    case 7: RemoveSprites(sprites); break;          /* nothing to import */
    case 8: Sg_PathJoin(p, sizeof p, dir, "TEAM3.DAT"); remove(p); break;
    case 9: Sg_PathJoin(p, sizeof p, dir, "SPRITE.DAT"); Sg_WriteFile(p, "x", 1, err); break;
    }
    err[0] = '\0';
    int32_t rc = Sg_DosSpritesImport(dir, sprites, err);
    CHECK(rc == -1 && err[0], "%s: import returned %d", what, (int)rc);
    printf("  %-22s -> %s\n", what, err);
    if (kind < 8)
        CHECK(SameGame(dosDir, dir), "%s: a failed import wrote", what);
}

int main(int argc, char **argv)
{
    char err[256];

    if (argc < 2) {
        printf("dos_sprites_test: no PC game folder given, skipped\n");
        return 0;
    }
    const char *dosDir = argv[1];

    RemoveGame(WORK "/orig");
    RemoveGame(WORK "/rt1");
    RemoveGame(WORK "/rt2");
    if (CopyGame(dosDir, WORK "/orig") != 0 || CopyGame(dosDir, WORK "/rt1") != 0 ||
        CopyGame(dosDir, WORK "/rt2") != 0)
        return 1;

    /* 1 */
    RemoveSprites(WORK "/a");
    CHECK(Sg_DosSpritesExport(WORK "/orig", WORK "/a", 0, err) == 0, "export: %s", err);
    CHECK(SameGame(dosDir, WORK "/orig"), "export wrote to the game folder");
    CHECK(Sg_DosSpritesImport(WORK "/rt1", WORK "/a", err) == 0, "import: %s", err);
    CheckRoundTrip(WORK "/orig", WORK "/rt1");

    /* 2 */
    RemoveSprites(WORK "/b");
    CHECK(Sg_DosSpritesExport(WORK "/rt1", WORK "/b", 0, err) == 0, "export 2: %s", err);
    CHECK(SameSprites(WORK "/a", WORK "/b") == 0, "the second export differs from the first");
    {
        char from[1024], to[1024];
        for (int32_t i = 0; i < NUM_FILES; i++) {
            Sg_PathJoin(from, sizeof from, WORK "/rt1", kFiles[i]);
            Sg_PathJoin(to, sizeof to, WORK "/rt2", kFiles[i]);
            TestCopyFile(from, to);
        }
    }
    CHECK(Sg_DosSpritesImport(WORK "/rt2", WORK "/b", err) == 0, "import 2: %s", err);
    CHECK(SameGame(WORK "/rt1", WORK "/rt2"), "export -> import is not a fixed point");

    /* An export with another background differs only in palette slot 16. */
    RemoveSprites(WORK "/c");
    CHECK(Sg_DosSpritesExport(WORK "/orig", WORK "/c", 13, err) == 0, "export bg 13: %s", err);
    CHECK(SameSprites(WORK "/a", WORK "/c") > 0, "-sprite-bg changed nothing");

    /* 3 */
    printf("malformed input:\n");
    ExpectImportFails(dosDir, "BMP cut short", 0);
    ExpectImportFails(dosDir, "4-bit BMP", 1);
    ExpectImportFails(dosDir, "top-down BMP", 2);
    ExpectImportFails(dosDir, "600 pixels wide", 3);
    ExpectImportFails(dosDir, "not a BMP", 4);
    ExpectImportFails(dosDir, "negative width", 5);
    ExpectImportFails(dosDir, "BMP header cut", 6);
    ExpectImportFails(dosDir, "no sprNNNN.bmp", 7);
    ExpectImportFails(dosDir, "TEAM3.DAT missing", 8);
    ExpectImportFails(dosDir, "SPRITE.DAT wrong size", 9);
    {
        char p[1024];
        Sg_PathJoin(p, sizeof p, WORK "/bad", "SPRITE.DAT");
        remove(p);
        err[0] = '\0';
        CHECK(Sg_DosSpritesExport(WORK "/bad", WORK "/d", 0, err) == -1 && err[0], "export without SPRITE.DAT");
        printf("  %-22s -> %s\n", "export, no SPRITE.DAT", err);
    }

    /* Leave the folders, but not the game's files or the BMPs. */
    RemoveGame(WORK "/orig"); RemoveGame(WORK "/rt1"); RemoveGame(WORK "/rt2"); RemoveGame(WORK "/bad");
    RemoveSprites(WORK "/a"); RemoveSprites(WORK "/b"); RemoveSprites(WORK "/c"); RemoveSprites(WORK "/badspr");
    remove(WORK "/badspr/SPR0400.BMP");

    printf(g_fail ? "dos_sprites_test: %d FAILED\n" : "dos_sprites_test: ok\n", (int)g_fail);
    return g_fail ? 1 : 0;
}
