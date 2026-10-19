/*
 * dos_pitch_render_test.c -- the -dos pitch mode (Sg_DosPitchRun) against the C#
 * SwosGfx's BMPs and Tiled maps for the same pitches.
 *
 *   dos_pitch_render_test
 *       only the checks on made-up pitches (missing and malformed files, a short
 *       pointer table), in ./dos_pitch_render_test.out
 *   dos_pitch_render_test DOSDIR OUTDIR [REFROOT]
 *       also every pitch type from the PC game in DOSDIR, into OUTDIR/<case>.
 *       With REFROOT, each case is compared file by file with REFROOT/<case>,
 *       made by SwosGfx.exe -dos -dos-dir=DOSDIR <options> REFROOT/<case>:
 *         bmp-<type>-<colors>          -output=bmp -type=<type> [-colors=N]
 *         tmj-<type>[-blank]-w<width>  -output=tmj -type=<type> -tilesetwidth=W
 *                                      [-insertblanktile]
 *         tmj-normal-c64               -output=tmj -type=normal -colors=64
 *       A case whose folder REFROOT lacks is skipped.
 */
#include "conv_compare.h"
#include "dos_pitch_render.h"

#include <stdlib.h>

static int32_t gWarnings;

static void CountWarning(void *user, const char *line)
{
    (void)user;
    (void)line;
    gWarnings++;
}

static void RunCase(const char *dosDir, const char *outRoot, const char *refRoot, const char *name,
                    int32_t type, int32_t colors, int32_t tiled, int32_t blank, int32_t width)
{
    char out[1024], ref[1024], err[256] = "";

    if (refRoot) {
        Sg_PathJoin(ref, sizeof ref, refRoot, name);
        if (!Sg_IsDir(ref))
            return;
    }
    Sg_PathJoin(out, sizeof out, outRoot, name);
    gWarnings = 0;
    CHECK(Sg_DosPitchRun(dosDir, out, type, colors, tiled, blank, width, CountWarning, NULL, err) == 0,
          "%s: %s", name, err);
    /* The PC game's pitch6 stands are not fans 1..22: SwosGfx warns once. */
    if (tiled)
        CHECK(gWarnings == 1, "%s: %d warnings", name, (int)gWarnings);
    if (refRoot)
        CompareFolders(ref, out);
}

/* ---- made-up pitches ------------------------------------------------------ */

static void WriteLe32s(const char *path, const uint32_t *v, int32_t n)
{
    uint8_t buf[64 * 4];
    char err[256];
    for (int32_t i = 0; i < n; i++) {
        buf[i * 4 + 0] = (uint8_t)v[i];
        buf[i * 4 + 1] = (uint8_t)(v[i] >> 8);
        buf[i * 4 + 2] = (uint8_t)(v[i] >> 16);
        buf[i * 4 + 3] = (uint8_t)(v[i] >> 24);
    }
    Sg_WriteFile(path, buf, (uint32_t)n * 4, err);
}

/* Six pitches of `n` pointers 0, 256, 512 ... over a bank of 24 tiles (tile t is
 * all t, so 1..22 are the fan pairs). */
static void MakeSet(const char *dir, int32_t n)
{
    static uint8_t blk[24 * 256];
    uint32_t ptr[64];
    char path[1024], name[32], err[256];

    for (int32_t t = 0; t < 24; t++)
        memset(blk + t * 256, t, 256);
    for (int32_t i = 0; i < n; i++)
        ptr[i] = (uint32_t)(i % 24) * 256;
    for (int32_t p = 1; p <= 6; p++) {
        snprintf(name, sizeof name, "PITCH%d.DAT", (int)p);
        Sg_PathJoin(path, sizeof path, dir, name);
        WriteLe32s(path, ptr, n);
        snprintf(name, sizeof name, "pitch%d.blk", (int)p);    /* any case */
        Sg_PathJoin(path, sizeof path, dir, name);
        Sg_WriteFile(path, blk, sizeof blk, err);
    }
}

static int32_t Fails(const char *dir, const char *outDir, int32_t tiled, const char *expect)
{
    char err[256] = "";
    int32_t rc = Sg_DosPitchRun(dir, outDir, 4, 256, tiled, 0, 256, NULL, NULL, err);
    if (rc == 0 || !strstr(err, expect)) {
        printf("  rc %d, err \"%s\", expected \"%s\"\n", (int)rc, err, expect);
        return 0;
    }
    return 1;
}

static void MadeUpChecks(const char *root)
{
    char dir[1024], out[1024], path[1024], err[256];
    SgBuf b;

    /* No folder at all. */
    Sg_PathJoin(dir, sizeof dir, root, "none");
    Sg_PathJoin(out, sizeof out, root, "out-none");
    CHECK(Fails(dir, out, 0, "Missing pattern index file: "), "missing folder");

    /* pitch4.blk missing. */
    Sg_PathJoin(dir, sizeof dir, root, "noblk");
    Sg_MakeDirs(dir);
    MakeSet(dir, 10);
    Sg_PathJoin(path, sizeof path, dir, "pitch4.blk");
    remove(path);
    CHECK(Fails(dir, out, 0, "Missing pattern data file: "), "missing blk");

    /* A .dat of 7 bytes. */
    Sg_PathJoin(dir, sizeof dir, root, "datlen");
    Sg_MakeDirs(dir);
    MakeSet(dir, 10);
    Sg_PathJoin(path, sizeof path, dir, "PITCH2.DAT");
    Sg_WriteFile(path, "\0\0\0\0\0\0\0", 7, err);
    CHECK(Fails(dir, out, 0, "length is not a multiple of 4."), "dat length");

    /* A .blk of 300 bytes. */
    Sg_PathJoin(dir, sizeof dir, root, "blklen");
    Sg_MakeDirs(dir);
    MakeSet(dir, 10);
    Sg_PathJoin(path, sizeof path, dir, "pitch6.blk");
    {
        static uint8_t junk[300];
        Sg_WriteFile(path, junk, sizeof junk, err);
    }
    CHECK(Fails(dir, out, 0, "length is not a multiple of 256 bytes"), "blk length");

    /* A pointer of 100: not on a tile. */
    Sg_PathJoin(dir, sizeof dir, root, "unaligned");
    Sg_MakeDirs(dir);
    MakeSet(dir, 10);
    {
        uint32_t ptr[3] = { 0, 100, 256 };
        Sg_PathJoin(path, sizeof path, dir, "PITCH1.DAT");
        WriteLe32s(path, ptr, 3);
    }
    CHECK(Fails(dir, out, 0, "pointer[1] = 100 is not aligned to 256 bytes."), "unaligned pointer");

    /* A pointer past the bank: loads, fails when drawn (both outputs). */
    Sg_PathJoin(dir, sizeof dir, root, "pastbank");
    Sg_MakeDirs(dir);
    MakeSet(dir, 10);
    {
        uint32_t ptr[3] = { 0, 0xFFFFFF00u, 256 };
        Sg_PathJoin(path, sizeof path, dir, "PITCH3.DAT");
        WriteLe32s(path, ptr, 3);
    }
    CHECK(Fails(dir, out, 0, "(Parameter 'patternIndex')"), "pointer past the bank (bmp)");
    CHECK(Fails(dir, out, 1, "(Parameter 'patternIndex')"), "pointer past the bank (tmj)");

    /* A short pointer table: the rest of the grid is index 0 / blank. 48
     * pointers reach into row 1, so pattern 1..6 of the first stand row become
     * crowd cells -- too few, so it warns. */
    Sg_PathJoin(dir, sizeof dir, root, "short");
    Sg_MakeDirs(dir);
    MakeSet(dir, 48);
    Sg_PathJoin(out, sizeof out, root, "out-short");
    {
        char e[256] = "";
        gWarnings = 0;
        CHECK(Sg_DosPitchRun(dir, out, 0, 16, 0, 0, 256, NULL, NULL, e) == 0, "short bmp: %s", e);
        Sg_PathJoin(path, sizeof path, out, "pitch1-frozen.bmp");
        CHECK(Sg_ReadFile(path, &b, e) == 0 && b.size == 14 + 40 + 1024 + 672 * 880, "short bmp size");
        Sg_BufFree(&b);
        /* The tileset overwrites that BMP: same name, as in SwosGfx. */
        CHECK(Sg_DosPitchRun(dir, out, 0, 256, 1, 1, 128, CountWarning, NULL, e) == 0, "short tmj: %s", e);
        CHECK(gWarnings == 6, "short tmj: %d warnings", (int)gWarnings);
        Sg_PathJoin(path, sizeof path, out, "pitch1-frozen.tmj");
        CHECK(Sg_ReadFile(path, &b, e) == 0, "short tmj");
        if (b.data) {
            /* blank + patterns 1..23 (pattern 0 is the blank tile); the crowd adds none. */
            CHECK(strstr((const char *)b.data, "\"tilecount\":24,") != NULL, "short tmj tile count");
            CHECK(strstr((const char *)b.data, "\"crowd-stand\"") != NULL, "short tmj crowd layers");
        }
        Sg_BufFree(&b);
    }
    /* Out of range arguments. */
    {
        char e[256] = "";
        CHECK(Sg_DosPitchRun(dir, out, 7, 256, 0, 0, 256, NULL, NULL, e) != 0, "pitch type 7");
        CHECK(Sg_DosPitchRun(dir, out, 0, 0, 0, 0, 256, NULL, NULL, e) != 0 &&
              strstr(e, "colorCount must be between 1 and 256"), "0 colours: %s", e);
    }
}

int main(int argc, char **argv)
{
    char made[1024];

    if (argc >= 3)
        Sg_PathJoin(made, sizeof made, argv[2], "made-up");
    else
        snprintf(made, sizeof made, "dos_pitch_render_test.out");
    Sg_MakeDirs(made);
    MadeUpChecks(made);

    if (argc < 3) {
        printf("dos_pitch_render_test: %d checks, %d failures (PC game part SKIPPED: give DOSDIR OUTDIR [REFROOT])\n",
               (int)gChecks, (int)gFailures);
        return gFailures ? 1 : 0;
    }

    const char *dosDir = argv[1], *outRoot = argv[2], *refRoot = argc > 3 && argv[3][0] ? argv[3] : NULL;
    static const int32_t colorCounts[] = { 256, 16, 64, 255 };
    static const int32_t widths[] = { 16, 128, 256, 384 };
    char name[128];

    for (int32_t t = 0; t < SG_DOS_PITCH_TYPE_COUNT; t++) {
        const char *tn = Sg_DosPitchTypeNames[t];
        for (int32_t c = 0; c < 4; c++) {
            snprintf(name, sizeof name, "bmp-%s-%d", tn, (int)colorCounts[c]);
            RunCase(dosDir, outRoot, refRoot, name, t, colorCounts[c], 0, 0, 256);
        }
        for (int32_t w = 0; w < 4; w++) {
            snprintf(name, sizeof name, "tmj-%s-w%d", tn, (int)widths[w]);
            RunCase(dosDir, outRoot, refRoot, name, t, 256, 1, 0, widths[w]);
            snprintf(name, sizeof name, "tmj-%s-blank-w%d", tn, (int)widths[w]);
            RunCase(dosDir, outRoot, refRoot, name, t, 256, 1, 1, widths[w]);
        }
    }
    RunCase(dosDir, outRoot, refRoot, "tmj-normal-c64", 4, 64, 1, 0, 256);
    RunCase(dosDir, outRoot, refRoot, "bmp-normal-pitch3", 4, 256, 0, 0, 256);

    printf("dos_pitch_render_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
