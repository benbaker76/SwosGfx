/*
 * dos2amiga_test.c -- -dos2amiga (Sg_Dos2AmigaRun): the eleven banks from the
 * PC game, against the C# SwosGfx's for the same folder.
 *
 *   dos2amiga_test                          skipped (needs the PC game)
 *   dos2amiga_test DOSDIR OUTDIR [REFDIR]   writes the banks to OUTDIR; with
 *                                           REFDIR (SwosGfx -dos -dos2amiga
 *                                           REFDIR) every file must match
 */
#include "conv_compare.h"
#include "dos2amiga.h"

int main(int argc, char **argv)
{
    char err[256] = "";

    if (argc < 3) {
        printf("dos2amiga_test: SKIP (give DOSDIR OUTDIR [REFDIR])\n");
        return 0;
    }
    CHECK(Sg_Dos2AmigaRun(argv[1], argv[2], PrintLine, NULL, err) == 0, "Sg_Dos2AmigaRun: %s", err);
    if (argc > 3)
        CompareFolders(argv[3], argv[2]);
    printf("dos2amiga_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
