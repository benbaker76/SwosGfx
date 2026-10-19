/*
 * amiga_tools_test.c -- the Amiga modes (amiga_tools.h): MAP <-> BMP / Tiled,
 * full-pitch BMP -> MAP, RAW <-> BMP, explicit and batch.
 *
 *   amiga_tools_test
 *       synthetic checks only (round trips and malformed inputs, in a folder
 *       "amiga_tools_test.tmp" under the current one); the game's files are
 *       skipped
 *   amiga_tools_test GRAFS GRAFS_AGA OUTDIR [REFDIR]
 *       also the case matrix below over the game's 4-plane (grafs) and 8-plane
 *       (grafs_aga) folders, each case's files in OUTDIR/<case>/; with REFDIR,
 *       every case must have done what the C# SwosGfx did: the same files byte
 *       for byte, and failed where it failed (REFDIR/<case>.rc holds its exit
 *       status)
 *   amiga_tools_test --list GRAFS GRAFS_AGA
 *       prints the matrix, one case a line, "name|arg|arg|...", the paths as
 *       placeholders {G} {A} (the two folders), {O} (the case's output folder)
 *       and {R} (the reference folder: the later cases convert the C#'s own
 *       outputs back). A script substitutes them and runs
 *       `SwosGfx.exe -amiga <args>` into REFDIR/<case>, writing its exit status
 *       to REFDIR/<case>.rc.
 *   amiga_tools_test --run <args>
 *       one SwosGfx-style command line (without -amiga), for trying things.
 */
#include "conv_compare.h"
#include "amiga_tools.h"
#include "amiga_raw.h"
#include "sg_image.h"
#include "tiled_map.h"

#include <stdarg.h>
#include <stdlib.h>

/* ---- a SwosGfx command line, the Amiga part ------------------------------ */

static void PrintErr(void *user, const char *line)
{
    (void)user;
    printf("[stderr] %s\n", line);
}

static int32_t Lower(int32_t c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int32_t StartsLower(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++)
        if (Lower((unsigned char)*s) != *prefix)
            return 0;
    return 1;
}

/* What a quiet run printed, as SwosGfx's console would show it. */
static char gCapture[8192];

static void Capture(void *user, const char *line)
{
    size_t n = strlen(gCapture);
    (void)user;
    snprintf(gCapture + n, sizeof gCapture - n, "%s\n", line);
}

/* Parse and run; 0 / 2 as SwosGfx's exit status. Quiet: the lines go to
 * gCapture, "Error: ..." included. */
static int32_t RunArgs(int32_t argc, char **argv, int32_t quiet)
{
    SgAmigaOptions o;
    const char *files[8];
    char err[256] = "";
    int32_t rc;

    Sg_AmigaOptionsInit(&o);
    o.files = files;
    for (int32_t i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (StartsLower(a, "-amiga-dir="))
            o.amigaDir = a + 11;
        else if (StartsLower(a, "-amiga") && !a[6])
            continue;
        else if (StartsLower(a, "-map") && !a[4])
            o.input = SG_AMIGA_KIND_MAP;
        else if (StartsLower(a, "-raw") && !a[4])
            o.input = SG_AMIGA_KIND_RAW;
        else if (StartsLower(a, "-bmp") && !a[4])
            o.input = SG_AMIGA_KIND_BMP;
        else if ((StartsLower(a, "-tmj") || StartsLower(a, "-tmx")) && !a[4])
            o.input = SG_AMIGA_KIND_TILED;
        else if (StartsLower(a, "-output=")) {
            const char *v = a + 8;
            o.output = StartsLower(v, "bmp") ? SG_AMIGA_KIND_BMP
                       : StartsLower(v, "map") ? SG_AMIGA_KIND_MAP
                       : StartsLower(v, "raw") ? SG_AMIGA_KIND_RAW
                                               : SG_AMIGA_KIND_TILED;
        } else if (StartsLower(a, "-palette="))
            o.paletteName = a + 9;
        else if (StartsLower(a, "-bitplanes=auto"))
            o.autoBitplanes = 1;
        else if (StartsLower(a, "-bitplanes=")) {
            o.bitplanes = atoi(a + 11);
            o.colorCount = 1 << o.bitplanes;
        } else if (StartsLower(a, "-colors="))
            o.colorCount = atoi(a + 8);
        else if (StartsLower(a, "-no-rnc"))
            o.noRnc = 1;
        else if (o.fileCount < 8)
            files[o.fileCount++] = a;
    }
    gCapture[0] = 0;
    o.log = quiet ? Capture : PrintLine;
    o.warn = quiet ? Capture : PrintErr;
    rc = Sg_AmigaRun(&o, err);
    if (rc != 0) {
        char line[300];
        snprintf(line, sizeof line, "Error: %s", err);
        if (quiet)
            Capture(NULL, line);
        else
            printf("%s\n", line);
        return 2;
    }
    return 0;
}

/* ---- the case matrix ----------------------------------------------------- */

typedef struct {
    char name[128];
    char args[1024];   /* '|'-separated, placeholders unexpanded */
} Case;

static Case *gCases;
static int32_t gMessageDiffs;
static int32_t gCaseCount, gCaseCap;

static void AddCase(const char *name, const char *args)
{
    if (gCaseCount == gCaseCap) {
        gCaseCap = gCaseCap ? gCaseCap * 2 : 256;
        gCases = realloc(gCases, (size_t)gCaseCap * sizeof *gCases);
    }
    snprintf(gCases[gCaseCount].name, sizeof gCases[0].name, "%s", name);
    snprintf(gCases[gCaseCount].args, sizeof gCases[0].args, "%s", args);
    gCaseCount++;
}

/* snprintf without GCC's truncation warnings for the matrix's short names. */
static void Fmt(char *out, size_t cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out, cap, fmt, ap);
    va_end(ap);
}

/* The base names (without extension) of `dir`'s files ending in `ext`. */
static int32_t Names(const char *dir, const char *ext, char names[][64], int32_t max)
{
    char **list = NULL;
    int32_t count = 0, n = 0;

    if (Sg_ListDir(dir, &list, &count) != 0)
        return 0;
    for (int32_t i = 0; i < count && n < max; i++) {
        size_t len = strlen(list[i]);
        if (!Sg_EndsWithNoCase(list[i], ext) || len - 4 >= 64)
            continue;
        snprintf(names[n], 64, "%.*s", (int)(len - 4), list[i]);
        n++;
    }
    Sg_ListDirFree(list, count);
    return n;
}

static void BuildMatrix(const char *grafs, const char *aga)
{
    static char gMaps[64][64], aMaps[64][64], gRaws[64][64], aRaws[64][64];
    int32_t ngm = Names(grafs, ".MAP", gMaps, 64), nam = Names(aga, ".MAP", aMaps, 64);
    int32_t ngr = Names(grafs, ".RAW", gRaws, 64), nar = Names(aga, ".RAW", aRaws, 64);
    char name[128], args[1024];

    /* Batch forms */
    AddCase("b_map_bmp_g", "-amiga-dir={G}|-map|-output=bmp|{O}");
    AddCase("b_map_bmp_g_bp8", "-amiga-dir={G}|-map|-output=bmp|-bitplanes=8|{O}");
    AddCase("b_map_bmp_g_frozen", "-amiga-dir={G}|-map|-output=bmp|-palette=frozen|{O}");
    AddCase("b_map_bmp_a", "-amiga-dir={A}|-map|-output=bmp|{O}");
    AddCase("b_map_bmp_a_bp8", "-amiga-dir={A}|-map|-output=bmp|-bitplanes=8|{O}");
    AddCase("b_map_bmp_a_bp8_menu", "-amiga-dir={A}|-map|-output=bmp|-bitplanes=8|-palette=menu|{O}");
    AddCase("b_map_tmj_g", "-amiga-dir={G}|-map|-output=tmj|{O}");
    AddCase("b_map_tmj_g_normal", "-amiga-dir={G}|-map|-output=tmj|-palette=normal|{O}");
    AddCase("b_map_tmj_a_bp8_dry", "-amiga-dir={A}|-map|-output=tmj|-bitplanes=8|-palette=dry|{O}");
    AddCase("b_raw_bmp_g", "-amiga-dir={G}|-raw|-output=bmp|{O}");
    AddCase("b_raw_bmp_g_c256", "-amiga-dir={G}|-raw|-output=bmp|-colors=256|{O}");
    AddCase("b_raw_bmp_g_menu", "-amiga-dir={G}|-raw|-output=bmp|-palette=menu|{O}");
    AddCase("b_raw_bmp_a", "-amiga-dir={A}|-raw|-output=bmp|{O}");
    AddCase("b_raw_bmp_a_bp8", "-amiga-dir={A}|-raw|-output=bmp|-bitplanes=8|{O}");
    AddCase("b_raw_bmp_a_bp8_menu", "-amiga-dir={A}|-raw|-output=bmp|-bitplanes=8|-palette=menu|{O}");
    AddCase("b_missing_dir", "-amiga-dir={G}/nonexistent|-raw|-output=bmp|{O}");

    /* MAP -> BMP, every map */
    for (int32_t i = 0; i < ngm; i++) {
        const char *m = gMaps[i];
#define MB(suffix, extra)                                                                 \
        Fmt(name, sizeof name, "mb_g_%s%s", m, suffix);                              \
        Fmt(args, sizeof args, "-map|-output=bmp%s|{G}/%s.MAP|{O}/%s.bmp", extra, m, m); \
        AddCase(name, args)
        MB("", "");
        MB("_wet", "|-palette=wet");
        MB("_menu_c256", "|-palette=menu|-colors=256");
        MB("_game_c32", "|-palette=game|-colors=32");
        MB("_bp8", "|-bitplanes=8");
        MB("_c64", "|-colors=64");
#undef MB
    }
    for (int32_t i = 0; i < nam; i++) {
        const char *m = aMaps[i];
#define MB(suffix, extra)                                                                 \
        Fmt(name, sizeof name, "mb_a_%s%s", m, suffix);                              \
        Fmt(args, sizeof args, "-map|-output=bmp%s|{A}/%s.MAP|{O}/%s.bmp", extra, m, m); \
        AddCase(name, args)
        MB("", "");
        MB("_bp8", "|-bitplanes=8");
        MB("_bp8_hard", "|-bitplanes=8|-palette=hard");
        MB("_bp8_c16", "|-bitplanes=8|-colors=16");
        MB("_bp8_service", "|-bitplanes=8|-palette=Service");
        MB("_bp8_titlelogo", "|-bitplanes=8|-palette=titlelogo");
#undef MB
    }

    /* MAP -> Tiled, every map */
    for (int32_t i = 0; i < ngm; i++) {
        const char *m = gMaps[i];
        Fmt(name, sizeof name, "mt_g_%s", m);
        Fmt(args, sizeof args, "-map|-output=tmj|-palette=normal|{G}/%s.MAP|{O}/%s.tmj", m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "mt_g_%s_noPal", m);
        Fmt(args, sizeof args, "-map|-output=tmj|{G}/%s.MAP|{O}/%s.tmj", m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "mt_g_%s_c256", m);
        Fmt(args, sizeof args, "-map|-output=tmj|-palette=soft|-colors=256|{G}/%s.MAP|{O}/%s.tmj", m, m);
        AddCase(name, args);
    }
    for (int32_t i = 0; i < nam; i++) {
        const char *m = aMaps[i];
        Fmt(name, sizeof name, "mt_a_%s", m);
        Fmt(args, sizeof args, "-map|-output=tmj|-bitplanes=8|-palette=game|{A}/%s.MAP|{O}/%s.tmj", m, m);
        AddCase(name, args);
    }

    /* Tiled -> MAP: the C#'s own maps back, packed and not, and at the other depth */
    for (int32_t i = 0; i < ngm; i++) {
        const char *m = gMaps[i];
        Fmt(name, sizeof name, "tm_g_%s", m);
        Fmt(args, sizeof args, "-tmj|-output=map|{R}/mt_g_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "tm_g_%s_norc", m);
        Fmt(args, sizeof args, "-tmj|-output=map|-no-rnc|{R}/mt_g_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "tm_g_%s_bp8", m);
        Fmt(args, sizeof args, "-tmj|-output=map|-bitplanes=8|{R}/mt_g_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "tm_g_%s_bp3", m);
        Fmt(args, sizeof args, "-tmj|-output=map|-bitplanes=3|{R}/mt_g_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
    }
    for (int32_t i = 0; i < nam; i++) {
        const char *m = aMaps[i];
        Fmt(name, sizeof name, "tm_a_%s", m);
        Fmt(args, sizeof args, "-tmj|-output=map|-bitplanes=8|{R}/mt_a_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "tm_a_%s_norc", m);
        Fmt(args, sizeof args, "-tmj|-output=map|-bitplanes=8|-no-rnc|{R}/mt_a_%s/%s.tmj|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
    }

    /* Full-pitch BMP -> MAP, from the C#'s map->bmp */
    for (int32_t i = 0; i < ngm; i++) {
        const char *m = gMaps[i];
        Fmt(name, sizeof name, "bm_g_%s", m);
        Fmt(args, sizeof args, "-bmp|-output=map|{R}/mb_g_%s/%s.bmp|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "bm_g_%s_norc", m);
        Fmt(args, sizeof args, "-bmp|-output=map|-no-rnc|{R}/mb_g_%s/%s.bmp|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "bm_g_%s_c256", m);
        Fmt(args, sizeof args, "-bmp|-output=map|{R}/mb_g_%s_menu_c256/%s.bmp|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
    }
    for (int32_t i = 0; i < nam; i++) {
        const char *m = aMaps[i];
        Fmt(name, sizeof name, "bm_a_%s", m);
        Fmt(args, sizeof args, "-bmp|-output=map|-bitplanes=8|{R}/mb_a_%s_bp8/%s.bmp|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
        Fmt(name, sizeof name, "bm_a_%s_norc_bp4", m);
        Fmt(args, sizeof args, "-bmp|-output=map|-no-rnc|{R}/mb_a_%s_bp8_c16/%s.bmp|{O}/%s.MAP", m, m, m);
        AddCase(name, args);
    }

    /* RAW -> BMP, every RAW */
    for (int32_t i = 0; i < ngr; i++) {
        const char *r = gRaws[i];
#define RB(suffix, extra)                                                                 \
        Fmt(name, sizeof name, "rb_g_%s%s", r, suffix);                              \
        Fmt(args, sizeof args, "-raw|-output=bmp%s|{G}/%s.RAW|{O}/%s.bmp", extra, r, r); \
        AddCase(name, args)
        RB("", "");
        RB("_c256", "|-colors=256");
        RB("_c48", "|-colors=48");
        RB("_titlelogo", "|-palette=titlelogo");
        RB("_game_c256", "|-palette=game|-colors=256");
        RB("_bp8", "|-bitplanes=8");
        RB("_bp2", "|-bitplanes=2");
#undef RB
    }
    for (int32_t i = 0; i < nar; i++) {
        const char *r = aRaws[i];
#define RB(suffix, extra)                                                                 \
        Fmt(name, sizeof name, "rb_a_%s%s", r, suffix);                              \
        Fmt(args, sizeof args, "-raw|-output=bmp%s|{A}/%s.RAW|{O}/%s.bmp", extra, r, r); \
        AddCase(name, args)
        RB("", "");
        RB("_bp8", "|-bitplanes=8");
        RB("_bp8_menu", "|-bitplanes=8|-palette=menu");
        RB("_bp8_game_c16", "|-bitplanes=8|-palette=game|-colors=16");
        RB("_bp8_normal", "|-bitplanes=8|-palette=normal");
#undef RB
    }

    /* BMP -> RAW, from the C#'s raw->bmp: round trips */
    for (int32_t i = 0; i < ngr; i++) {
        const char *r = gRaws[i];
        Fmt(name, sizeof name, "br_g_%s", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|{R}/rb_g_%s/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_g_%s_norc", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-no-rnc|{R}/rb_g_%s/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_g_%s_auto", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=auto|{R}/rb_g_%s_c256/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_g_%s_bp8", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=8|-no-rnc|{R}/rb_g_%s_c256/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_g_%s_bp4_c256", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=4|{R}/rb_g_%s_c256/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
    }
    for (int32_t i = 0; i < nar; i++) {
        const char *r = aRaws[i];
        Fmt(name, sizeof name, "br_a_%s_auto", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=auto|{R}/rb_a_%s_bp8_menu/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_a_%s_auto_norc", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=auto|-no-rnc|-palette=menu|{R}/rb_a_%s_bp8_menu/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
        Fmt(name, sizeof name, "br_a_%s_bp8", r);
        Fmt(args, sizeof args, "-bmp|-output=raw|-bitplanes=8|-palette=menu|{R}/rb_a_%s_bp8_menu/%s.bmp|{O}/%s.RAW", r, r, r);
        AddCase(name, args);
    }
}

/* Replace the placeholders of `in` into out. */
static void Expand(char *out, size_t cap, const char *in, const char *g, const char *a, const char *o,
                   const char *r)
{
    size_t n = 0;
    for (; *in && n + 1 < cap; in++) {
        const char *v = NULL;
        if (in[0] == '{' && in[1] && in[2] == '}') {
            v = in[1] == 'G' ? g : in[1] == 'A' ? a : in[1] == 'O' ? o : in[1] == 'R' ? r : NULL;
        }
        if (v) {
            n += (size_t)snprintf(out + n, cap - n, "%s", v);
            if (n >= cap)
                n = cap - 1;
            in += 2;
        } else
            out[n++] = *in;
    }
    out[n] = 0;
}

static int32_t RunCase(const Case *c, const char *grafs, const char *aga, const char *outRoot, const char *refRoot)
{
    char outDir[1024], line[4096];
    char *argv[32];
    int32_t argc = 0;

    Sg_PathJoin(outDir, sizeof outDir, outRoot, c->name);
    Sg_MakeDirs(outDir);
    Expand(line, sizeof line, c->args, grafs, aga, outDir, refRoot ? refRoot : outRoot);
    for (char *p = strtok(line, "|"); p && argc < 32; p = strtok(NULL, "|"))
        argv[argc++] = p;
    return RunArgs(argc, argv, 1);
}

/* ---- synthetic checks (no game data) ------------------------------------- */

static int32_t WriteBytes(const char *path, const void *data, uint32_t size)
{
    char err[256];
    return Sg_WriteFile(path, data, size, err);
}

static void Synthetic(const char *tmp)
{
    SgAmigaTools t = { 1, NULL, NULL };
    SgAmigaTools tn = { 0, NULL, NULL };
    char err[256], p1[1024], p2[1024], p3[1024], p4[1024];
    uint32_t pal[16];
    SgImage img, back;
    SgBuf a, b;

    Sg_MakeDirs(tmp);
    for (int32_t i = 0; i < 16; i++)
        pal[i] = 0xFF000000u | (uint32_t)(i * 0x111111);

    /* A pitch of 40 distinct tiles, 4 bits: BMP -> MAP -> BMP keeps it. */
    SgImage_Init(&img, 672, 880);
    for (int32_t y = 0; y < 880; y++)
        for (int32_t x = 0; x < 672; x++) {
            int32_t tile = ((y / 16) * 42 + x / 16) % 40;
            img.px[y * 672 + x] = (uint8_t)((tile + x * 3 + (y % 16)) & 15);
        }
    SgImage_SetPaletteArgb(&img, pal, 16);
    Sg_PathJoin(p1, sizeof p1, tmp, "pitch.bmp");
    Sg_PathJoin(p2, sizeof p2, tmp, "pitch.map");
    Sg_PathJoin(p3, sizeof p3, tmp, "pitch2.bmp");
    CHECK(SgImage_WriteBmp(&img, p1, err) == 0, "%s", err);
    for (int32_t planes = 4; planes <= 8; planes += 4) {
        for (int32_t rnc = 0; rnc < 2; rnc++) {
            CHECK(Sg_AmigaConvertFullPitchBmpToMap(rnc ? &t : &tn, p1, p2, 284, planes, err) == 0, "%s", err);
            CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p2, pal, 16, p3, NULL, 0, planes, 16, err) == 0, "%s", err);
            CHECK(SgImage_ReadBmp(&back, p3, err) == 0, "%s", err);
            CHECK(back.w == 672 && back.h == 880 && memcmp(back.px, img.px, 672 * 880) == 0,
                  "pitch round trip at %d planes, rnc %d", (int)planes, (int)rnc);
            SgImage_Free(&back);
        }
    }

    /* MAP -> Tiled -> MAP: the same MAP. */
    Sg_PathJoin(p3, sizeof p3, tmp, "tiles.tmj");   /* its tileset: tiles.bmp */
    Sg_PathJoin(p4, sizeof p4, tmp, "pitch3.map");
    CHECK(Sg_AmigaConvertFullPitchBmpToMap(&tn, p1, p2, 284, 4, err) == 0, "%s", err);
    CHECK(Sg_AmigaConvertPitchMapToTiled(&t, p2, pal, 16, p3, NULL, 0, 4, 16, err) == 0, "%s", err);
    CHECK(Sg_AmigaConvertTiledToPitchMap(&tn, p3, p4, 284, 4, err) == 0, "%s", err);
    CHECK(Sg_ReadFile(p2, &a, err) == 0 && Sg_ReadFile(p4, &b, err) == 0 && a.size == b.size &&
              memcmp(a.data, b.data, a.size) == 0, "map -> tmj -> map");
    Sg_BufFree(&a);
    Sg_BufFree(&b);

    /* A legacy AGA map (offsets counted in 128-byte tiles) still reads. */
    {
        SgBuf m;
        CHECK(Sg_AmigaConvertFullPitchBmpToMap(&tn, p1, p2, 284, 8, err) == 0, "%s", err);
        CHECK(Sg_ReadFile(p2, &m, err) == 0, "%s", err);
        for (int32_t c = 0; c < 42 * 55; c++) {
            uint32_t off = Sg_Be32(m.data + c * 4) / 2;
            m.data[c * 4] = (uint8_t)(off >> 24); m.data[c * 4 + 1] = (uint8_t)(off >> 16);
            m.data[c * 4 + 2] = (uint8_t)(off >> 8); m.data[c * 4 + 3] = (uint8_t)off;
        }
        Sg_PathJoin(p4, sizeof p4, tmp, "legacy.map");
        WriteBytes(p4, m.data, m.size);
        Sg_PathJoin(p3, sizeof p3, tmp, "legacy.bmp");
        CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p4, pal, 16, p3, NULL, 0, 8, 16, err) == 0, "%s", err);
        CHECK(SgImage_ReadBmp(&back, p3, err) == 0 && memcmp(back.px, img.px, 672 * 880) == 0, "legacy map");
        SgImage_Free(&back);
        /* the same map at the wrong depth: not a multiple, and a hint */
        CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p4, pal, 16, p3, NULL, 0, 3, 8, err) != 0 &&
                  strstr(err, "-bitplanes=") != NULL, "wrong depth: %s", err);
        /* a cell pointing past the tiles */
        m.data[0] = 0x7F;
        WriteBytes(p4, m.data, m.size);
        CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p4, pal, 16, p3, NULL, 0, 8, 16, err) != 0, "bad cell");
        /* cut short */
        WriteBytes(p4, m.data, 9000);
        CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p4, pal, 16, p3, NULL, 0, 8, 16, err) != 0, "short map");
        Sg_BufFree(&m);
    }
    SgImage_Free(&img);

    /* RAW round trip, 320x256 at 4 planes, packed and not. */
    SgImage_Init(&img, 320, 256);
    for (int32_t i = 0; i < 320 * 256; i++)
        img.px[i] = (uint8_t)((i * 7 + i / 320) & 15);
    SgImage_SetPaletteArgb(&img, pal, 16);
    Sg_PathJoin(p1, sizeof p1, tmp, "pic.bmp");
    Sg_PathJoin(p2, sizeof p2, tmp, "pic.raw");
    Sg_PathJoin(p3, sizeof p3, tmp, "pic2.bmp");
    CHECK(SgImage_WriteBmp(&img, p1, err) == 0, "%s", err);
    for (int32_t rnc = 0; rnc < 2; rnc++) {
        CHECK(Sg_AmigaConvertBmpToRaw(rnc ? &t : &tn, p1, p2, 0, err) == 0, "%s", err);
        CHECK(Sg_AmigaConvertRawToBmp(&t, p2, pal, 16, p3, NULL, 0, 4, 16, err) == 0, "%s", err);
        CHECK(SgImage_ReadBmp(&back, p3, err) == 0 && memcmp(back.px, img.px, 320 * 256) == 0, "raw round trip");
        SgImage_Free(&back);
    }
    /* the wrong depth for its size; too many colours for the palette */
    CHECK(Sg_AmigaConvertRawToBmp(&t, p2, pal, 16, p3, NULL, 0, 5, 16, err) != 0 && strstr(err, "Unknown raw"),
          "raw size: %s", err);
    CHECK(Sg_AmigaConvertRawToBmp(&t, p2, pal, 16, p3, NULL, 0, 4, 32, err) != 0, "colorCount 32 of 16");
    /* an index too big for the depth */
    img.px[5] = 200;
    CHECK(SgImage_WriteBmp(&img, p1, err) == 0, "%s", err);
    CHECK(Sg_AmigaConvertBmpToRaw(&t, p1, p2, 4, err) != 0 && strstr(err, "exceeds maximum"), "depth: %s", err);
    SgImage_Free(&img);

    /* garbage: not a BMP, a broken RNC header, an empty file, a missing file */
    Sg_PathJoin(p4, sizeof p4, tmp, "junk.bin");
    WriteBytes(p4, "RNC\x07garbage-garbage-garbage", 26);
    CHECK(Sg_AmigaConvertRawToBmp(&t, p4, pal, 16, p3, NULL, 0, 4, 16, err) != 0, "junk RNC raw");
    CHECK(Sg_AmigaConvertPitchMapToBmp(&t, p4, NULL, 0, p3, NULL, 0, 4, 16, err) != 0, "junk RNC map");
    CHECK(Sg_AmigaConvertBmpToRaw(&t, p4, p2, 4, err) != 0, "junk bmp");
    CHECK(Sg_AmigaConvertFullPitchBmpToMap(&t, p4, p2, 284, 4, err) != 0, "junk bmp");
    CHECK(Sg_AmigaConvertTiledToPitchMap(&t, p4, p2, 284, 4, err) != 0, "junk tmj");
    WriteBytes(p4, "BM", 2);
    CHECK(Sg_AmigaConvertBmpToRaw(&t, p4, p2, 4, err) != 0, "2-byte bmp");
    WriteBytes(p4, "", 0);
    CHECK(Sg_AmigaConvertRawToBmp(&t, p4, pal, 16, p3, NULL, 0, 4, 16, err) != 0, "empty raw");
    CHECK(Sg_AmigaConvertRawToBmp(&t, "/nonexistent/x.raw", pal, 16, p3, NULL, 0, 4, 16, err) != 0, "missing");
    CHECK(Sg_AmigaConvertBmpToRaw(&t, p1, p2, 9, err) != 0, "9 planes");
    /* a full-pitch BMP too small */
    SgImage_Init(&img, 64, 64);
    CHECK(SgImage_WriteBmp(&img, p1, err) == 0, "%s", err);
    CHECK(Sg_AmigaConvertFullPitchBmpToMap(&t, p1, p2, 284, 4, err) != 0, "small pitch");
    SgImage_Free(&img);

    /* a Tiled map with an empty cell, and one whose tileset is missing */
    {
        SgTiledMap tm;
        SgTiledMap_Init(&tm);
        SgTiledMap_SetSize(&tm, 42, 55, 16, 16, err);
        tm.tileCount = 1;
        tm.columns = 16;
        tm.imageWidth = 256;
        tm.imageHeight = 16;
        SgTiled_SetString(&tm.imageSource, "nothere.bmp");
        for (int32_t c = 0; c < 42 * 55; c++)
            tm.gids[c] = 1;
        Sg_PathJoin(p4, sizeof p4, tmp, "bad.tmj");
        CHECK(SgTiledMap_Write(&tm, p4, err) == 0, "%s", err);
        Sg_PathJoin(p1, sizeof p1, tmp, "nothere.bmp");
        remove(p1);   /* from an earlier run */
        CHECK(Sg_AmigaConvertTiledToPitchMap(&t, p4, p2, 284, 4, err) != 0, "missing tileset");
        SgImage_Init(&img, 256, 16);
        SgImage_WriteBmp(&img, p1, err);
        SgImage_Free(&img);
        CHECK(Sg_AmigaConvertTiledToPitchMap(&t, p4, p2, 284, 4, err) == 0, "%s", err);
        tm.gids[7] = 0;
        CHECK(SgTiledMap_Write(&tm, p4, err) == 0, "%s", err);
        CHECK(Sg_AmigaConvertTiledToPitchMap(&t, p4, p2, 284, 4, err) != 0 && strstr(err, "GID 0"), "gid 0");
        tm.gids[7] = 5;
        tm.columns = 0;
        CHECK(SgTiledMap_Write(&tm, p4, err) == 0, "%s", err);
        CHECK(Sg_AmigaConvertTiledToPitchMap(&t, p4, p2, 284, 4, err) != 0, "columns 0");
        SgTiledMap_Free(&tm);
    }

    /* the palette rules */
    {
        SgAmigaOptions o;
        uint32_t pl[256], np[256];
        int32_t pc, nc;
        Sg_AmigaOptionsInit(&o);
        CHECK(Sg_AmigaGetPaletteRequired(&o, "x/LOADER00.RAW", pl, &pc, np, &nc, err) == 0 && pc == 16 && nc == 0,
              "LOADER00");
        o.colorCount = 256;
        CHECK(Sg_AmigaGetPaletteRequired(&o, "olddisk1.raw", pl, &pc, np, &nc, err) == 0 && nc == 256, "DISK");
        CHECK(Sg_AmigaGetPaletteRequired(&o, "CREDITS.RAW", pl, &pc, np, &nc, err) != 0, "CREDITS");
        o.paletteName = "nonsense";
        CHECK(Sg_AmigaGetPaletteRequired(&o, "MENUS.RAW", pl, &pc, np, &nc, err) != 0, "bad palette");
    }

    /* a batch that finds nothing warns and succeeds */
    {
        SgAmigaOptions o;
        const char *files[1];
        Sg_AmigaOptionsInit(&o);
        Sg_PathJoin(p4, sizeof p4, tmp, "empty");
        Sg_MakeDirs(p4);
        Sg_PathJoin(p3, sizeof p3, tmp, "batchout");
        files[0] = p3;
        o.files = files;
        o.fileCount = 1;
        o.amigaDir = p4;
        o.input = SG_AMIGA_KIND_RAW;
        o.output = SG_AMIGA_KIND_BMP;
        o.warn = PrintErr;
        CHECK(Sg_AmigaRun(&o, err) == 0, "%s", err);
        o.output = SG_AMIGA_KIND_RAW;
        CHECK(Sg_AmigaRun(&o, err) != 0, "batch to raw");
    }
}

int main(int argc, char **argv)
{
    int32_t fails = 0, cases = 0;

    if (argc > 1 && !strcmp(argv[1], "--run"))
        return RunArgs(argc - 2, argv + 2, 0);
    if (argc > 3 && !strcmp(argv[1], "--list")) {
        BuildMatrix(argv[2], argv[3]);
        for (int32_t i = 0; i < gCaseCount; i++)
            printf("%s|%s\n", gCases[i].name, gCases[i].args);
        return 0;
    }

    Synthetic("amiga_tools_test.tmp");

    if (argc < 4) {
        printf("amiga_tools_test: game data SKIP (give GRAFS GRAFS_AGA OUTDIR [REFDIR])\n");
    } else {
        const char *ref = argc > 4 ? argv[4] : NULL;
        BuildMatrix(argv[1], argv[2]);
        for (int32_t i = 0; i < gCaseCount; i++) {
            const Case *c = &gCases[i];
            int32_t rc = RunCase(c, argv[1], argv[2], argv[3], ref);
            cases++;
            if (ref) {
                char rcPath[1024], refDir[1024], outDir[1024], name[160];
                SgBuf rb;
                char err[256];
                int32_t refRc = -1, before = gFailures;
                Fmt(name, sizeof name, "%s.rc", c->name);
                Sg_PathJoin(rcPath, sizeof rcPath, ref, name);
                if (Sg_ReadFile(rcPath, &rb, err) == 0) {
                    refRc = atoi((const char *)rb.data);
                    Sg_BufFree(&rb);
                }
                Sg_PathJoin(refDir, sizeof refDir, ref, c->name);
                Sg_PathJoin(outDir, sizeof outDir, argv[3], c->name);
                CHECK(refRc == rc, "%s: exit %d, the C#'s %d", c->name, (int)rc, (int)refRc);
                {
                    /* The console text: progress lines must match; an error's
                     * wording is reported, not failed (some are .NET's own). */
                    char outPath[1024], text[8192];
                    size_t n = 0;
                    snprintf(name, sizeof name, "%s.out", c->name);
                    Sg_PathJoin(outPath, sizeof outPath, ref, name);
                    if (Sg_ReadFile(outPath, &rb, err) == 0) {
                        for (uint32_t k = 0; k < rb.size && n + 1 < sizeof text; k++)
                            if (rb.data[k] != '\r')
                                text[n++] = (char)rb.data[k];
                        Sg_BufFree(&rb);
                    }
                    text[n] = 0;
                    if (strcmp(text, gCapture) != 0) {
                        int32_t isError = strstr(text, "Error: ") != NULL && rc != 0;
                        if (isError)
                            gMessageDiffs++;
                        else
                            CHECK(0, "%s: console differs", c->name);
                        printf("  %s %s:\n    C#:   %s    ours: %s", isError ? "message" : "CONSOLE", c->name,
                               text, gCapture);
                    }
                }
                {
                    /* quiet CompareFolders: only its failures print */
                    char **rl = NULL, **ol = NULL;
                    int32_t nr = 0, no = 0;
                    Sg_ListDir(refDir, &rl, &nr);
                    Sg_ListDir(outDir, &ol, &no);
                    CHECK(nr == no, "%s: %d files, the C# %d", c->name, (int)no, (int)nr);
                    for (int32_t k = 0; k < nr; k++)
                        CHECK(SameFile(refDir, outDir, rl[k]), "%s/%s differs", c->name, rl[k]);
                    Sg_ListDirFree(rl, nr);
                    Sg_ListDirFree(ol, no);
                }
                if (gFailures != before)
                    fails++;
            }
        }
        printf("amiga_tools_test: %d cases%s, %d differ, %d error messages worded differently\n", (int)cases,
               ref ? " compared" : "", (int)fails, (int)gMessageDiffs);
    }
    printf("amiga_tools_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    free(gCases);
    return gFailures ? 1 : 0;
}
