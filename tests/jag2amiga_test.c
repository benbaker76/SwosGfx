/*
 * jag2amiga_test.c -- -jag2amiga (Sg_Jag2AmigaRun) against the C# SwosGfx's
 * output for the same cartridge, PC game and Jaguar folder.
 *
 *   jag2amiga_test       skipped (needs the cartridge and the PC game)
 *   jag2amiga_test DOSDIR ROM JAGDIR OUTDIR [REFDIR [template] [sheets]]
 *       writes everything to OUTDIR; with REFDIR (SwosGfx -dos -jag2amiga
 *       -jag-rom=ROM -jag-dir=JAGDIR [-jag-template] [-jag-dump-sheets] REFDIR)
 *       every file must match. "template" / "sheets" switch those outputs on.
 *       JAGDIR may be "-" for none.
 */
#include "conv_compare.h"
#include "jag2amiga.h"

int main(int argc, char **argv)
{
    char err[256] = "";
    int32_t tmpl = 0, sheets = 0;

    if (argc < 5) {
        printf("jag2amiga_test: SKIP (give DOSDIR ROM JAGDIR OUTDIR [REFDIR [template] [sheets]])\n");
        return 0;
    }
    for (int32_t i = 6; i < argc; i++) {
        if (!strcmp(argv[i], "template"))
            tmpl = 1;
        else if (!strcmp(argv[i], "sheets"))
            sheets = 1;
    }
    CHECK(Sg_Jag2AmigaRun(argv[1], argv[2], strcmp(argv[3], "-") ? argv[3] : NULL, argv[4], tmpl, sheets,
                          PrintLine, NULL, err) == 0, "Sg_Jag2AmigaRun: %s", err);
    if (argc > 5)
        CompareFolders(argv[5], argv[4]);
    printf("jag2amiga_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
