/*
 * palettes_test.c -- the -palettes export and the palette lookup by name.
 *
 *   palettes_test [refDir [resolveDir]]
 *
 * Writes the export for every option combination (-pal-color x -pal-file x
 * -pal-count x -pal-full, and every -pal-format with -pal-file=palette: 84)
 * into a temporary folder, one subfolder per combination:
 * <color>_<file>_<count>_<part|full>[_<format>], e.g. rgb32_palette_128_full_gimp.
 * With `refDir`, holding the same subfolders written by
 *   SwosGfx -palettes -pal-color=<color> -pal-file=<file> -pal-count=<count>
 *           -pal-format=<format> [-pal-full] <refDir>/<subfolder>
 * (-pal-format=act for asm and c), each must hold the same file names with
 * the same bytes.
 *
 * With `resolveDir`, Sg_ResolvePalette is checked against SwosGfx's
 * ResolvePalette as its BMPs show it: <name>_<count>.bmp, made by
 *   SwosGfx -amiga -raw -output=bmp -palette=<name> -bitplanes=8 -colors=<count>
 * on an all-zero picture (-bitplanes=1 for count 2). The BMP's palette is the
 * resolved one cut to `count` (QuantizeBmp), zeros after it. Where SwosGfx
 * fails (a 16-colour palette asked for more, an unknown name) there is no BMP
 * and <name>_<count>.log holds its message; an unknown name's must be ours.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "amiga_palette.h"
#include "dos_palette.h"
#include "pal_file.h"
#include "sg_util.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ---- file names in a folder, sorted -------------------------------------- */

typedef struct {
    char    names[64][128];
    int32_t count;
} NameList;

static int cmp_name(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static int32_t list_dir(const char *dir, NameList *l)
{
    l->count = 0;
#ifdef _WIN32
    char pat[1024];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (l->count < 64)
            snprintf(l->names[l->count++], 128, "%.127s", fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        if (l->count < 64)
            snprintf(l->names[l->count++], 128, "%.127s", e->d_name);
    }
    closedir(d);
#endif
    qsort(l->names, (size_t)l->count, sizeof l->names[0], cmp_name);
    return 0;
}

static void remove_files(const char *dir)
{
    NameList l;
    char path[1024];
    if (list_dir(dir, &l) != 0)
        return;
    for (int32_t i = 0; i < l.count; i++) {
        Sg_PathJoin(path, sizeof path, dir, l.names[i]);
        remove(path);
    }
}

/* ---- the export ---------------------------------------------------------- */

static int32_t g_files, g_combos, g_failures;

static void fail(const char *fmt, const char *a, const char *b)
{
    printf("FAIL ");
    printf(fmt, a, b);
    printf("\n");
    g_failures++;
}

static void compare_dirs(const char *mine, const char *ref, const char *combo)
{
    NameList a, b;
    char pa[1024], pb[1024], err[256];

    if (list_dir(ref, &b) != 0) {
        fail("%s: no reference folder %s", combo, ref);
        return;
    }
    list_dir(mine, &a);
    if (a.count != b.count) {
        fail("%s: file count differs from %s", combo, ref);
        return;
    }
    for (int32_t i = 0; i < a.count; i++) {
        SgBuf fa = { 0 }, fb = { 0 };
        if (strcmp(a.names[i], b.names[i]) != 0) {
            fail("%s: file names differ (%s)", combo, a.names[i]);
            continue;
        }
        Sg_PathJoin(pa, sizeof pa, mine, a.names[i]);
        Sg_PathJoin(pb, sizeof pb, ref, b.names[i]);
        if (Sg_ReadFile(pa, &fa, err) != 0 || Sg_ReadFile(pb, &fb, err) != 0)
            fail("%s: cannot read %s", combo, a.names[i]);
        else if (fa.size != fb.size || memcmp(fa.data, fb.data, fa.size) != 0)
            fail("%s: %s differs", combo, a.names[i]);
        else
            g_files++;
        Sg_BufFree(&fa);
        Sg_BufFree(&fb);
    }
}

static void run_export(const char *tmp, const char *refDir)
{
    static const int32_t counts[3] = { 16, 128, 256 };
    char combo[128], out[1024], ref[1024], err[256];

    for (int32_t c = 0; c < SG_PAL_COLOR_COUNT; c++)
    for (int32_t f = 0; f < SG_PAL_FILE_COUNT; f++)
    for (int32_t n = 0; n < 3; n++)
    for (int32_t full = 0; full < 2; full++)
    for (int32_t fmt = 0; fmt < SG_PAL_FORMAT_COUNT; fmt++) {
        if (f != SG_PAL_FILE_PALETTE && fmt != 0)
            continue;   /* the format only matters for palette files */

        snprintf(combo, sizeof combo, "%s_%s_%d_%s", Sg_PalColorNames[c], Sg_PalFileNames[f],
                 (int)counts[n], full ? "full" : "part");
        if (f == SG_PAL_FILE_PALETTE) {
            size_t l = strlen(combo);
            snprintf(combo + l, sizeof combo - l, "_%s", Sg_PalFormatNames[fmt]);
        }
        Sg_PathJoin(out, sizeof out, tmp, combo);
        remove_files(out);

        if (Sg_PalettesExport(out, (SgPalColor)c, (SgPalFile)f, counts[n], full,
                              (SgPalFormat)fmt, err) != 0) {
            fail("%s: %s", combo, err);
            continue;
        }
        g_combos++;

        NameList l;
        if (list_dir(out, &l) != 0 || l.count != 10)
            fail("%s: expected 10 files in %s", combo, out);

        if (refDir) {
            Sg_PathJoin(ref, sizeof ref, refDir, combo);
            compare_dirs(out, ref, combo);
        }
    }
}

/* ---- the lookup ---------------------------------------------------------- */

static int32_t g_resolved, g_throws;

static void check_resolve(const char *resolveDir)
{
    /* The spellings SwosGfx was run with: mixed case on purpose. */
    static const char *const names[] = {
        "Frozen", "muddy", "WET", "Soft", "Normal", "Dry", "Hard",
        "Menu", "game", "Service", "TitleLogo", "titletext", "TITLESCREEN", "Bogus"
    };
    static const int32_t counts[] = { 2, 16, 17, 128, 256 };
    char key[64], path[1024], err[256], rerr[256], text[64];

    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        for (size_t k = 0; k < sizeof counts / sizeof counts[0]; k++) {
            uint32_t pal[256];
            int32_t  palCount = 0;
            size_t   j;
            SgBuf    bmp = { 0 };

            for (j = 0; names[i][j] && j < sizeof key - 1; j++)
                key[j] = (char)((names[i][j] >= 'A' && names[i][j] <= 'Z') ? names[i][j] + 32 : names[i][j]);
            key[j] = '\0';
            snprintf(text, sizeof text, "%s %d", names[i], (int)counts[k]);

            int32_t rc = Sg_ResolvePalette(names[i], counts[k], pal, &palCount, err);

            snprintf(path, sizeof path, "%s/%s_%d.bmp", resolveDir, key, (int)counts[k]);
            if (Sg_ReadFile(path, &bmp, rerr) != 0) {
                /* SwosGfx failed: the log says why. */
                SgBuf log = { 0 };
                snprintf(path, sizeof path, "%s/%s_%d.log", resolveDir, key, (int)counts[k]);
                if (Sg_ReadFile(path, &log, rerr) != 0) {
                    fail("%s: no BMP and no log in %s", text, resolveDir);
                    continue;
                }
                if (rc != 0) {
                    /* "Error: <message>\r\n" */
                    char expect[300];
                    snprintf(expect, sizeof expect, "Error: %s", err);
                    if (log.size < strlen(expect) || memcmp(log.data, expect, strlen(expect)) != 0)
                        fail("%s: error message differs: %s", text, err);
                    else
                        g_resolved++;
                } else if (counts[k] > palCount) {
                    g_throws++;     /* QuantizeBmp refuses more colours than the palette */
                } else {
                    fail("%s: SwosGfx failed, we did not%s", text, "");
                }
                Sg_BufFree(&log);
                continue;
            }

            if (rc != 0) {
                fail("%s: %s", text, err);
            } else if (bmp.size < 54 + 1024) {
                fail("%s: short BMP%s", text, "");
            } else {
                int32_t keep = counts[k] < palCount ? counts[k] : palCount;
                int32_t ok = 1;
                for (int32_t e = 0; e < 256; e++) {
                    const uint8_t *p = bmp.data + 54 + e * 4;
                    uint32_t got = ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
                    uint32_t want = e < keep ? (pal[e] & 0xFFFFFF) : 0;
                    if (got != want)
                        ok = 0;
                }
                if (ok)
                    g_resolved++;
                else
                    fail("%s: palette differs%s", text, "");
            }
            Sg_BufFree(&bmp);
        }
    }
}

int main(int argc, char **argv)
{
    const char *refDir = argc > 1 ? argv[1] : NULL;
    const char *resolveDir = argc > 2 ? argv[2] : NULL;
    const char *t = getenv("TMPDIR");
    char tmp[1024];

#ifdef _WIN32
    if (!t)
        t = getenv("TEMP");
#endif
    Sg_PathJoin(tmp, sizeof tmp, t ? t : "/tmp", "swosgfx_palettes_test");

    run_export(tmp, refDir);
    printf("export: %d combinations, %d files compared%s, into %s\n", (int)g_combos,
           (int)g_files, refDir ? "" : " (no reference given)", tmp);

    if (resolveDir) {
        check_resolve(resolveDir);
        printf("resolve: %d name x count cases equal SwosGfx's, %d where SwosGfx's QuantizeBmp "
               "refuses the count\n", (int)g_resolved, (int)g_throws);
    }

    printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", (int)g_failures);
    return g_failures ? 1 : 0;
}
