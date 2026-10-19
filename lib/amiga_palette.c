/*
 * amiga_palette.c -- SwosGfx's palettes; see amiga_palette.h.
 *
 * AmigaPalette.cs (tables, conversions, GetNearestColor, QuantizeBmp,
 * PaletteToHex/PaletteToCode/PaletteToPalette, OutputAllPalettes) and
 * Program.cs's ResolvePalette, with the C# names. GetColorDistance's CIEDE2000
 * branch is not ported: nothing in SwosGfx asks for it.
 */
#include "amiga_palette.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dos_palette.h"
#include "sg_util.h"

/* StringBuilder.AppendLine on the Windows reference: the .s/.c files must equal
 * its files byte for byte on every host. */
#define EOL "\r\n"

const char *const Sg_PalColorNames[SG_PAL_COLOR_COUNT] = { "amiga12", "rgb32" };
const char *const Sg_PalFileNames[SG_PAL_FILE_COUNT]   = { "asm", "c", "palette" };

const char *const Sg_AmigaPitchPaletteNames[SG_AMIGA_PITCH_PALETTES] = {
    "Frozen", "Muddy", "Wet", "Soft", "Normal", "Dry", "Hard"
};

const uint16_t Sg_AmigaPaletteMenu[16] = {
    0x0001, 0x0AAB, 0x0FFF, 0x0102, 0x0621, 0x0A40, 0x0F71, 0x0667,
    0x0204, 0x0445, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
};

const uint16_t Sg_AmigaPaletteGame[16] = {
    0x0360, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0250,
    0x0030, 0x0370, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0
};

const uint16_t Sg_AmigaPaletteService[16] = {
    0x0000, 0x0AAA, 0x0FFF, 0x0F00, 0x0E90, 0x0EE0, 0x00F0, 0x000F,
    0x07A7, 0x00DF, 0x0666, 0x0DC0, 0x00CF, 0x0F7B, 0x0F0F, 0x0CDF
};

const uint16_t Sg_AmigaPaletteTitleLogo[16] = {
    0x0002, 0x0A02, 0x0FB2, 0x0333, 0x043A, 0x0082, 0x0CCC, 0x0888,
    0x0555, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332
};

const uint16_t Sg_AmigaPaletteTitleText[16] = {
    0x0002, 0x0F04, 0x0F04, 0x0004, 0x0F04, 0x0F04, 0x0F04, 0x0F04,
    0x0006, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332
};

const uint16_t Sg_AmigaPaletteTitleScreen[16] = {
    0x0002, 0x0002, 0x0222, 0x0444, 0x0777, 0x0999, 0x0CCC, 0x0EEE,
    0x003A, 0x008C, 0x0EC3, 0x0802, 0x0A02, 0x0D02, 0x0062, 0x0FFF
};

const uint16_t Sg_AmigaPalettePanic[16] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x00FF
};

/* The game palette with entries 0, 7 and 9 (the grass) recoloured per pitch. */
const uint16_t Sg_AmigaPalettePitches[SG_AMIGA_PITCH_PALETTES][16] = {
    /* Frozen */
    { 0x0996, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0896,
      0x0030, 0x0796, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Muddy */
    { 0x0750, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0650,
      0x0030, 0x0550, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Wet */
    { 0x0390, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0790,
      0x0030, 0x0390, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Soft */
    { 0x0370, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0770,
      0x0030, 0x0370, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Normal */
    { 0x0790, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0690,
      0x0030, 0x0990, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Dry */
    { 0x0990, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0890,
      0x0030, 0x0790, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
    /* Hard */
    { 0x0970, 0x0999, 0x0FFF, 0x0000, 0x0721, 0x0A40, 0x0F71, 0x0870,
      0x0030, 0x0770, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0 },
};

/* ---- conversions --------------------------------------------------------- */

uint16_t Sg_AmigaToAmiga12(uint32_t argb)
{
    int32_t r8 = (int32_t)((argb >> 16) & 0xFF);
    int32_t g8 = (int32_t)((argb >> 8) & 0xFF);
    int32_t b8 = (int32_t)(argb & 0xFF);

    /* Truncate to the upper nibble. */
    return (uint16_t)(((r8 >> 4) << 8) | ((g8 >> 4) << 4) | (b8 >> 4));
}

uint32_t Sg_AmigaFromAmiga12(uint16_t amiga)
{
    int32_t r4 = (amiga >> 8) & 0xF;
    int32_t g4 = (amiga >> 4) & 0xF;
    int32_t b4 = (amiga >> 0) & 0xF;

    /* << 4 rather than * 17, to match SwosGfx's PC palettes and screenshots. */
    return 0xFF000000u | ((uint32_t)(r4 << 4) << 16) | ((uint32_t)(g4 << 4) << 8) | (uint32_t)(b4 << 4);
}

void Sg_AmigaPaletteToAmiga12(const uint32_t *argb, int32_t count, uint16_t *out)
{
    for (int32_t i = 0; i < count; i++)
        out[i] = Sg_AmigaToAmiga12(argb[i]);
}

void Sg_AmigaPaletteFromAmiga12(const uint16_t *amiga, int32_t count, uint32_t *out)
{
    for (int32_t i = 0; i < count; i++)
        out[i] = Sg_AmigaFromAmiga12(amiga[i]);
}

int32_t Sg_AmigaBitplanesToColors(int32_t bitplanes)
{
    if (bitplanes < 1 || bitplanes > 8)
        return -1;
    return 1 << bitplanes;
}

/* GetColorDistance(Sqrt). The C#'s min-distance bookkeeping inside it is a
 * no-op for one pair, so it is the plain distance. */
static double GetColorDistance(uint32_t argb1, uint32_t argb2)
{
    int32_t dr = (int32_t)((argb1 >> 16) & 0xFF) - (int32_t)((argb2 >> 16) & 0xFF);
    int32_t dg = (int32_t)((argb1 >> 8) & 0xFF) - (int32_t)((argb2 >> 8) & 0xFF);
    int32_t db = (int32_t)(argb1 & 0xFF) - (int32_t)(argb2 & 0xFF);
    return sqrt((double)(dr * dr + dg * dg + db * db));
}

double Sg_AmigaGetNearestColor(uint32_t argb, const uint32_t *pal, int32_t count,
                               int32_t *nearestIndex)
{
    double minDistance = 1.7976931348623157e308;   /* double.MaxValue */
    *nearestIndex = -1;

    for (int32_t i = 0; i < count; i++) {
        double distance = GetColorDistance(argb, pal[i]);
        if (distance < minDistance) {
            minDistance = distance;
            *nearestIndex = i;
        }
    }
    return minDistance;
}

int32_t Sg_AmigaQuantizeToPalette(uint8_t *indices, int32_t w, int32_t h,
                                  const uint32_t *pal, int32_t palCount,
                                  const uint32_t *newPal, int32_t newCount, char *err)
{
    int32_t colorMap[256];

    if (palCount > 256)
        palCount = 256;     /* an 8-bit index cannot reach further */

    for (int32_t i = 0; i < palCount; i++) {
        /* A colour already at the same index in the new palette keeps it. */
        if (i < newCount && pal[i] == newPal[i]) {
            colorMap[i] = i;
            continue;
        }
        int32_t nearestIndex;
        Sg_AmigaGetNearestColor(pal[i], newPal, newCount, &nearestIndex);
        if (nearestIndex < 0) {
            snprintf(err, 256, "Nearest color not found for palette index %d.", (int)i);
            return -1;
        }
        colorMap[i] = nearestIndex;
    }

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            uint8_t *p = &indices[(size_t)y * (size_t)w + (size_t)x];
            int32_t idx = *p;
            if (idx == 0)       /* transparent */
                continue;
            if (idx >= palCount) {
                snprintf(err, 256, "Pixel index %d at (%d,%d) is outside palette length %d.",
                         (int)idx, (int)x, (int)y, (int)palCount);
                return -1;
            }
            *p = (uint8_t)colorMap[idx];
        }
    }
    return 0;
}

int32_t Sg_AmigaQuantizeToCount(uint8_t *indices, int32_t w, int32_t h,
                                const uint32_t *pal, int32_t palCount,
                                int32_t colorCount, char *err)
{
    int32_t colorMap[256];

    if (colorCount <= 0 || colorCount > palCount) {
        snprintf(err, 256, "colorCount must be between 1 and %d, got %d.", (int)palCount, (int)colorCount);
        return -1;
    }
    if (palCount > 256)
        palCount = 256;

    /* The kept entries are the first colorCount; map the rest onto them. */
    for (int32_t i = colorCount; i < palCount; i++) {
        int32_t nearestIndex;
        Sg_AmigaGetNearestColor(pal[i], pal, colorCount, &nearestIndex);
        colorMap[i] = nearestIndex;
    }

    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            uint8_t *p = &indices[(size_t)y * (size_t)w + (size_t)x];
            int32_t idx = *p;
            if (idx == 0)
                continue;
            if (idx >= palCount) {
                snprintf(err, 256, "Pixel index %d at (%d,%d) is outside palette length %d.",
                         (int)idx, (int)x, (int)y, (int)palCount);
                return -1;
            }
            if (idx < colorCount)
                continue;
            *p = (uint8_t)colorMap[idx];
        }
    }
    return 0;
}

/* ---- palettes by name ---------------------------------------------------- */

const char *const Sg_PaletteNames[SG_PALETTE_NAME_COUNT] = {
    "Frozen", "Muddy", "Wet", "Soft", "Normal", "Dry", "Hard",
    "Menu", "Game", "Service", "TitleLogo", "TitleText", "TitleScreen"
};

static int32_t eq_nocase(const char *a, const char *b, size_t blen)
{
    if (strlen(b) != blen)
        return 0;
    for (size_t i = 0; i < blen; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

int32_t Sg_ResolvePalette(const char *paletteName, int32_t colorCount, uint32_t out[256],
                          int32_t *outCount, char *err)
{
    if (!paletteName) {
        snprintf(err, 256, "no palette name");
        return -1;
    }

    /* paletteName.Trim().ToLowerInvariant() */
    const char *key = paletteName;
    size_t len = strlen(key);
    while (len && isspace((unsigned char)*key)) {
        key++;
        len--;
    }
    while (len && isspace((unsigned char)key[len - 1]))
        len--;

    for (int32_t i = 0; i < SG_AMIGA_PITCH_PALETTES; i++) {
        if (!eq_nocase(key, Sg_AmigaPitchPaletteNames[i], len))
            continue;
        if (colorCount > 16) {
            Sg_DosGetPitchPalette(i, out);
            *outCount = 256;
        } else {
            Sg_AmigaPaletteFromAmiga12(Sg_AmigaPalettePitches[i], 16, out);
            *outCount = 16;
        }
        return 0;
    }

    static const struct {
        const char     *name;
        const uint16_t *amiga;       /* 16 entries */
        const uint32_t *dos;         /* NULL: the Amiga one at any colour count */
        int32_t         dosCount;
    } named[] = {
        { "menu",        Sg_AmigaPaletteMenu,        Sg_DosPaletteMenu,    256 },
        { "game",        Sg_AmigaPaletteGame,        Sg_DosPaletteGame,    256 },
        { "service",     Sg_AmigaPaletteService,     Sg_DosPaletteService, 16 },
        { "titlelogo",   Sg_AmigaPaletteTitleLogo,   NULL, 0 },
        { "titletext",   Sg_AmigaPaletteTitleText,   NULL, 0 },
        { "titlescreen", Sg_AmigaPaletteTitleScreen, NULL, 0 },
    };

    for (size_t n = 0; n < sizeof named / sizeof named[0]; n++) {
        if (!eq_nocase(key, named[n].name, len))
            continue;
        if (colorCount > 16 && named[n].dos) {
            memcpy(out, named[n].dos, (size_t)named[n].dosCount * sizeof(uint32_t));
            *outCount = named[n].dosCount;
        } else {
            Sg_AmigaPaletteFromAmiga12(named[n].amiga, 16, out);
            *outCount = 16;
        }
        return 0;
    }

    snprintf(err, 256, "Unknown palette '%.100s'. Valid names: Frozen, Muddy, Wet, Soft, Normal, Dry, Hard.",
             paletteName);
    return -1;
}

/* ---- code text (PaletteToHex / PaletteToCode) ---------------------------- */

typedef struct {
    char     *data;
    uint32_t  size, cap;
    int32_t   failed;
} PalText;

static void pt_add(PalText *t, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (t->failed)
        return;
    if (t->size + n + 1 > t->cap) {
        uint32_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap < t->size + n + 1)
            cap *= 2;
        char *d = (char *)realloc(t->data, cap);
        if (!d) {
            t->failed = 1;
            return;
        }
        t->data = d;
        t->cap = cap;
    }
    memcpy(t->data + t->size, s, n + 1);
    t->size += n;
}

/* Comma-separated hex, eight to a line, the comma before the line break (or
 * none there when !eolComma, as dc.w wants). `wide` picks X8 (ARGB) over X4. */
static void PaletteToHex(PalText *t, const uint16_t *amiga, const uint32_t *argb, int32_t count,
                         const char *valuePrefix, int32_t eolComma, const char *linePrefix)
{
    const int32_t perLine = 8;
    char hex[16];

    for (int32_t i = 0; i < count; i++) {
        if (i % perLine == 0) {
            if (i != 0)
                pt_add(t, EOL);
            pt_add(t, linePrefix);
        }

        pt_add(t, valuePrefix);
        if (argb)
            snprintf(hex, sizeof hex, "%08X", (unsigned)argb[i]);
        else
            snprintf(hex, sizeof hex, "%04X", (unsigned)amiga[i]);
        pt_add(t, hex);

        if (i != count - 1) {
            if (i % perLine == perLine - 1) {
                if (eolComma)
                    pt_add(t, ",");
            } else {
                pt_add(t, ",");
            }
        }
    }
    pt_add(t, EOL);
}

/* Both PaletteToCode overloads: 12-bit (`amiga`, "unsigned short") or ARGB
 * (`argb`, "unsigned int"). The ARGB one under asm is still dc.w of X8 values,
 * as the C# writes it. FileFormat.Palette lands here too when the colours are
 * 12-bit, and then gets C's prefixes without C's declaration -- the C#'s
 * ternaries test only for Asm and C. */
static void PaletteToCode(PalText *t, const char *name, const uint16_t *amiga, const uint32_t *argb,
                          int32_t count, SgPalFile fileFormat)
{
    int32_t     isAsm = (fileFormat == SG_PAL_FILE_ASM);
    const char *commentPrefix = isAsm ? ";" : "//";
    const char *hexPrefix = isAsm ? "$" : "0x";
    const char *linePrefix = isAsm ? "dc.w " : "    ";
    int32_t     eolComma = !isAsm;

    pt_add(t, commentPrefix);
    pt_add(t, " ");
    pt_add(t, name);
    pt_add(t, EOL);

    if (fileFormat == SG_PAL_FILE_C) {
        pt_add(t, argb ? "unsigned int " : "unsigned short ");
        pt_add(t, name);
        pt_add(t, "[] = {" EOL);
    }

    PaletteToHex(t, amiga, argb, count, hexPrefix, eolComma, linePrefix);

    if (fileFormat == SG_PAL_FILE_C)
        pt_add(t, "};" EOL);
}

/* GetFileName: outDir/fileName, or fileName alone without a folder. */
static void GetFileName(char *out, uint32_t cap, const char *outDir, const char *fileName)
{
    Sg_PathJoin(out, cap, outDir ? outDir : "", fileName);
}

static int32_t write_code(const char *outDir, const char *name, const char *extension,
                          const uint16_t *amiga, const uint32_t *argb, int32_t count,
                          SgPalFile fileFormat, char *err)
{
    PalText t = { 0 };
    char fileName[256], path[1024];
    int32_t rc;

    PaletteToCode(&t, name, amiga, argb, count, fileFormat);
    snprintf(fileName, sizeof fileName, "%s%s", name, extension);
    GetFileName(path, sizeof path, outDir, fileName);

    if (t.failed) {
        snprintf(err, 256, "out of memory writing %.200s", path);
        rc = -1;
    } else {
        rc = Sg_WriteFile(path, t.data, t.size, err);
    }
    free(t.data);
    return rc;
}

int32_t Sg_AmigaPaletteToPalette(const char *outDir, const char *name,
                                 const uint32_t *argb, int32_t count,
                                 SgPalFormat format, char *err)
{
    const char *extension = Sg_PalFormatExtension(format);
    char fileName[256], path[1024];

    if (!extension) {
        snprintf(err, 256, "Unsupported palette format.");
        return -1;
    }
    snprintf(fileName, sizeof fileName, "%s%s", name, extension);
    GetFileName(path, sizeof path, outDir, fileName);
    return Sg_PalFileWrite(path, argb, count, -1, format, err);
}

/* ---- -palettes ----------------------------------------------------------- */

/* The ten palettes in the C#'s order: Menu, Game, Service, then the pitches. */
#define NUM_OUTPUT 10

static const char *output_name(int32_t k)
{
    static const char *const fixed[3] = { "Menu", "Game", "Service" };
    return k < 3 ? fixed[k] : Sg_AmigaPitchPaletteNames[k - 3];
}

static const uint16_t *output_amiga(int32_t k)
{
    static const uint16_t *const fixed[3] = { Sg_AmigaPaletteMenu, Sg_AmigaPaletteGame, Sg_AmigaPaletteService };
    return k < 3 ? fixed[k] : Sg_AmigaPalettePitches[k - 3];
}

/* The PC palette behind output k at `colorCount` colours: X.Take(colorCount)
 * for Menu/Game/Service (Service has only 16) and a full pitch, and a pitch's
 * GetPitchPalette(i).Skip(PaletteIndices[i]).Take(9) otherwise. That Skip is by
 * the PITCH's number into the index table (0, 7, 9, 78, 79, 80, 81 for Frozen..
 * Hard), not the nine recoloured entries; it is the C#'s, kept. */
static int32_t output_dos(int32_t k, int32_t colorCount, int32_t full, uint32_t out[256])
{
    const uint32_t *src;
    int32_t         srcCount;
    uint32_t        pitch[256];

    if (k < 3) {
        src = k == 0 ? Sg_DosPaletteMenu : k == 1 ? Sg_DosPaletteGame : Sg_DosPaletteService;
        srcCount = k == 2 ? 16 : 256;
    } else {
        Sg_DosGetPitchPalette(k - 3, pitch);
        src = pitch;
        srcCount = 256;
        if (!full) {
            int32_t skip = Sg_DosPaletteIndices[k - 3];
            memcpy(out, pitch + skip, 9 * sizeof(uint32_t));
            return 9;
        }
    }

    int32_t n = colorCount < srcCount ? colorCount : srcCount;
    memcpy(out, src, (size_t)n * sizeof(uint32_t));
    return n;
}

/* OutputAllPalettes. Its four branches (Amiga12 / RGB32 x 16 / more colours)
 * come down to: at 16 colours the Amiga tables, written as 12-bit code -- under
 * RGB32 too, the C# passes the ushort tables -- or, for RGB32 palette files,
 * widened; above 16 the PC tables, cut to 12 bits for Amiga12 code, as they are
 * for RGB32. Files are written in the C#'s order, so a failure leaves the same
 * ones behind. */
int32_t Sg_PalettesExport(const char *outDir, SgPalColor colorFormat, SgPalFile fileFormat,
                          int32_t colorCount, int32_t fullPalettes, SgPalFormat paletteFormat,
                          char *err)
{
    const char *extension = (fileFormat == SG_PAL_FILE_ASM) ? ".s" : ".c";

    if (colorFormat < 0 || colorFormat >= SG_PAL_COLOR_COUNT) {
        snprintf(err, 256, "Invalid palette color format.");
        return -1;
    }
    if (fileFormat < 0 || fileFormat >= SG_PAL_FILE_COUNT) {
        snprintf(err, 256, "Invalid palette file format.");
        return -1;
    }
    if (colorCount != 16 && colorCount != 128 && colorCount != 256) {
        snprintf(err, 256, "Invalid palette color count '%d'. Expected 16, 128, or 256.", (int)colorCount);
        return -1;
    }

    /* CreateDirectory(outDir) */
    if (outDir && outDir[0] && Sg_MakeDirs(outDir) != 0) {
        snprintf(err, 256, "cannot create the folder %.200s", outDir);
        return -1;
    }

    for (int32_t k = 0; k < NUM_OUTPUT; k++) {
        const char *name = output_name(k);
        uint32_t    argb[256];
        uint16_t    amiga[256];
        int32_t     n, rc;

        if (colorCount == 16) {
            if (colorFormat == SG_PAL_COLOR_RGB32 && fileFormat == SG_PAL_FILE_PALETTE) {
                Sg_AmigaPaletteFromAmiga12(output_amiga(k), 16, argb);
                rc = Sg_AmigaPaletteToPalette(outDir, name, argb, 16, paletteFormat, err);
            } else {
                rc = write_code(outDir, name, extension, output_amiga(k), NULL, 16, fileFormat, err);
            }
        } else {
            n = output_dos(k, colorCount, fullPalettes, argb);
            if (colorFormat == SG_PAL_COLOR_AMIGA12) {
                Sg_AmigaPaletteToAmiga12(argb, n, amiga);
                rc = write_code(outDir, name, extension, amiga, NULL, n, fileFormat, err);
            } else if (fileFormat == SG_PAL_FILE_PALETTE) {
                rc = Sg_AmigaPaletteToPalette(outDir, name, argb, n, paletteFormat, err);
            } else {
                rc = write_code(outDir, name, extension, NULL, argb, n, fileFormat, err);
            }
        }
        if (rc != 0)
            return -1;
    }
    return 0;
}
