/*
 * dos_picture_test.c -- -dos -picture (Sg_DosPictureRun) against the C#
 * SwosGfx's BMPs for the same pictures.
 *
 *   dos_picture_test                 skipped (needs the PC game)
 *   dos_picture_test DOSDIR OUTDIR [REFDIR] [WxH | scale | batch]
 *       every *.256 in DOSDIR, one call each by its bare name, into the folder
 *       OUTDIR (scaled to WxH, or 352x272 for "scale"); "batch" makes one call
 *       for the whole folder instead. With REFDIR (SwosGfx -dos -picture
 *       -output=bmp [-scale[=WxH]] NAME.256 REFDIR for each), every file must
 *       match.
 */
#include "conv_compare.h"
#include "dos_picture.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    char err[256] = "";
    const char *mode = argc > 4 ? argv[4] : "";
    int32_t w = 0, h = 0;

    if (argc < 3) {
        printf("dos_picture_test: SKIP (give DOSDIR OUTDIR [REFDIR] [WxH|scale|batch])\n");
        return 0;
    }
    if (!strcmp(mode, "scale")) {
        w = SG_AMIGA_SCREEN_W;
        h = SG_AMIGA_SCREEN_H;
    } else if (mode[0] && strcmp(mode, "batch") && sscanf(mode, "%dx%d", &w, &h) != 2) {
        printf("bad size %s\n", mode);
        return 2;
    }
    if (!strcmp(mode, "batch")) {
        Sg_MakeDirs(argv[2]);
        CHECK(Sg_DosPictureRun(argv[1], NULL, argv[2], 0, 0, PrintLine, NULL, err) == 0,
              "Sg_DosPictureRun (batch): %s", err);
    } else {
        char **names = NULL;
        int32_t n = 0;
        CHECK(Sg_ListDir(argv[1], &names, &n) == 0, "cannot list %s", argv[1]);
        for (int32_t i = 0; i < n; i++) {
            if (!Sg_EndsWithNoCase(names[i], ".256"))
                continue;
            CHECK(Sg_DosPictureRun(argv[1], names[i], argv[2], w, h, PrintLine, NULL, err) == 0,
                  "Sg_DosPictureRun %s: %s", names[i], err);
        }
        Sg_ListDirFree(names, n);
    }
    /* Not a picture: SwosGfx's message. */
    CHECK(Sg_DosPictureRun(argv[1], "NO_SUCH.256", argv[2], 0, 0, NULL, NULL, err) != 0 &&
          !strcmp(err, "Failed to load DOS picture 'NO_SUCH.256': File"), "missing file: %s", err);
    if (argc > 3 && argv[3][0])
        CompareFolders(argv[3], argv[2]);
    printf("dos_picture_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
