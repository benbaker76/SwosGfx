/*
 * conv_compare.h -- for the tests of the converter modes (dos2amiga, jag2amiga,
 * dos_picture, font_extend): compare every file of a reference folder (the C#
 * SwosGfx's output for the same inputs) with ours, byte for byte.
 */
#ifndef SWOSGFX_CONV_COMPARE_H
#define SWOSGFX_CONV_COMPARE_H

#include "sg_dir.h"
#include "sg_util.h"

#include <stdio.h>
#include <string.h>

static int32_t gFailures, gChecks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        gChecks++;                                          \
        if (!(cond)) {                                      \
            gFailures++;                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);     \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static void PrintLine(void *user, const char *line)
{
    (void)user;
    printf("%s\n", line);
}

/* Is the file `name` in both folders, byte for byte? */
static int32_t SameFile(const char *refDir, const char *ourDir, const char *name)
{
    char a[1024], b[1024], err[256];
    SgBuf x, y;
    int32_t same;

    Sg_PathJoin(a, sizeof a, refDir, name);
    Sg_PathJoin(b, sizeof b, ourDir, name);
    if (Sg_ReadFile(a, &x, err) != 0)
        return 0;
    if (Sg_ReadFile(b, &y, err) != 0) {
        Sg_BufFree(&x);
        printf("  missing  %s\n", name);
        return 0;
    }
    same = x.size == y.size && (x.size == 0 || memcmp(x.data, y.data, x.size) == 0);
    if (!same) {
        uint32_t i = 0;
        while (i < x.size && i < y.size && x.data[i] == y.data[i])
            i++;
        printf("  differs  %s (sizes %u / %u, first at byte %u)\n", name, (unsigned)x.size,
               (unsigned)y.size, (unsigned)i);
    }
    Sg_BufFree(&x);
    Sg_BufFree(&y);
    return same;
}

/* Every file of refDir against ourDir, and no file of ours the reference has
 * not got. Prints and counts; returns the number of files compared. */
static int32_t CompareFolders(const char *refDir, const char *ourDir)
{
    char **ref = NULL, **ours = NULL;
    int32_t nref = 0, nours = 0, same = 0;

    CHECK(Sg_ListDir(refDir, &ref, &nref) == 0, "cannot list %s", refDir);
    CHECK(Sg_ListDir(ourDir, &ours, &nours) == 0, "cannot list %s", ourDir);
    for (int32_t i = 0; i < nref; i++) {
        int32_t ok = SameFile(refDir, ourDir, ref[i]);
        CHECK(ok, "%s differs from the reference", ref[i]);
        same += ok;
    }
    for (int32_t i = 0; i < nours; i++) {
        int32_t found = 0;
        for (int32_t k = 0; k < nref && !found; k++)
            found = strcmp(ours[i], ref[k]) == 0;
        CHECK(found, "%s is not in the reference", ours[i]);
    }
    printf("  %d / %d files identical to %s\n", (int)same, (int)nref, refDir);
    Sg_ListDirFree(ref, nref);
    Sg_ListDirFree(ours, nours);
    return nref;
}

#endif
