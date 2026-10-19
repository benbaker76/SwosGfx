/*
 * font_extend_test.c -- -font-extend (Sg_FontExtendRun) against the C#
 * SwosGfx's output for the same CHARSET and glyphs.
 *
 *   font_extend_test                     skipped (needs a CHARSET bmp)
 *   font_extend_test WORKDIR [REFDIR] [noboxes] [inplace]
 *       WORKDIR holds CHARSET.bmp and glyphs.txt. Writes WORKDIR/OUT.bmp (or,
 *       with "inplace", over CHARSET.bmp) and WORKDIR/integration.txt. With
 *       REFDIR (the same folder after SwosGfx -font-extend
 *       -glyphs=REFDIR/glyphs.txt [-no-boxes] REFDIR/CHARSET.bmp [REFDIR/OUT.bmp])
 *       every file must match.
 */
#include "conv_compare.h"
#include "font_extend.h"

int main(int argc, char **argv)
{
    char err[256] = "", in[1024], out[1024], glyphs[1024];
    int32_t boxes = 1, inplace = 0;

    if (argc < 2) {
        printf("font_extend_test: SKIP (give WORKDIR [REFDIR] [noboxes] [inplace])\n");
        return 0;
    }
    for (int32_t i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "noboxes"))
            boxes = 0;
        else if (!strcmp(argv[i], "inplace"))
            inplace = 1;
    }
    Sg_PathJoin(in, sizeof in, argv[1], "CHARSET.bmp");
    Sg_PathJoin(out, sizeof out, argv[1], "OUT.bmp");
    Sg_PathJoin(glyphs, sizeof glyphs, argv[1], "glyphs.txt");
    CHECK(Sg_FontExtendRun(in, inplace ? NULL : out, glyphs, boxes, 1, PrintLine, NULL, err) == 0,
          "Sg_FontExtendRun: %s", err);
    CHECK(Sg_FontExtendRun(out, NULL, "no/such/glyphs.txt", 1, 1, NULL, NULL, err) != 0 &&
          !strcmp(err, "glyphs file not found: no/such/glyphs.txt"), "missing glyphs: %s", err);
    if (argc > 2 && argv[2][0])
        CompareFolders(argv[2], argv[1]);
    printf("font_extend_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
