/*
 * amiga_raw.c -- Amiga planar RAW pictures (see amiga_raw.h for the layout).
 *
 * Ported from SwosGfx: AmigaTools.cs (ConvertRawToBmp, ConvertBmpToRaw,
 * DetectRawDimensions), Program.cs (RunAmiga's GetPaletteRequired) and
 * AmigaPalette.cs (FromAmiga12, GetNearestColor, QuantizeBmp).
 */
#include "amiga_raw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- palettes ------------------------------------------------------------ */

/* AmigaPalette.cs Menu, TitleLogo, TitleText, TitleScreen (12-bit 0x0RGB). */
const uint16_t Sg_AmigaPal12Menu[16] = {
    0x0001, 0x0AAB, 0x0FFF, 0x0102, 0x0621, 0x0A40, 0x0F71, 0x0667,
    0x0204, 0x0445, 0x0F00, 0x000F, 0x0702, 0x088F, 0x0380, 0x0FF0,
};
const uint16_t Sg_AmigaPal12TitleLogo[16] = {
    0x0002, 0x0A02, 0x0FB2, 0x0333, 0x043A, 0x0082, 0x0CCC, 0x0888,
    0x0555, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332,
};
const uint16_t Sg_AmigaPal12TitleText[16] = {
    0x0002, 0x0F04, 0x0F04, 0x0004, 0x0F04, 0x0F04, 0x0F04, 0x0F04,
    0x0006, 0x0FF9, 0x0DD8, 0x0BB7, 0x0996, 0x0774, 0x0553, 0x0332,
};
const uint16_t Sg_AmigaPal12TitleScreen[16] = {
    0x0002, 0x0002, 0x0222, 0x0444, 0x0777, 0x0999, 0x0CCC, 0x0EEE,
    0x003A, 0x008C, 0x0EC3, 0x0802, 0x0A02, 0x0D02, 0x0062, 0x0FFF,
};

/* DosPalette.cs Menu ("SWOS palette during menus") and Game, alpha dropped. */
const uint32_t Sg_AmigaRawDosMenuPal[256] = {
    0x000024, 0xB4B4B4, 0xFCFCFC, 0x000000, 0x6C2400, 0xB44800, 0xFC6C00, 0x6C6C6C,
    0x242424, 0x484848, 0xFC0000, 0x0000FC, 0x6C0024, 0x9090FC, 0x249000, 0xFCFC00,
    0x907854, 0x9C845C, 0xA88C68, 0xB49874, 0xC0A480, 0xCCB08C, 0xD8C098, 0xE4CCA8,
    0xE4D0B0, 0xE8D4BC, 0xECDCC4, 0xF0E0D0, 0xF0E8D8, 0xF4ECE4, 0xF8F4F0, 0xFCFCFC,
    0x000000, 0x000008, 0x000010, 0x000018, 0x000020, 0x000028, 0x000030, 0x000038,
    0x000040, 0x000048, 0x000050, 0x000058, 0x000060, 0x000068, 0x000070, 0x000078,
    0x000084, 0x00008C, 0x000094, 0x00009C, 0x0000A4, 0x0000AC, 0x0000B4, 0x0000BC,
    0x0000C4, 0x0000CC, 0x0000D4, 0x0000DC, 0x0000E4, 0x0000EC, 0x0000F4, 0x0000FC,
    0x6C2400, 0x702400, 0x742800, 0x782C00, 0x7C3000, 0x803400, 0x883800, 0x8C3800,
    0x903C00, 0x944000, 0x984400, 0x9C4800, 0xA44C00, 0xA85000, 0xAC5800, 0xB05C00,
    0xB46000, 0xB86400, 0xC06800, 0xC47000, 0xC87400, 0xCC7800, 0xD07C00, 0xD48400,
    0xD88800, 0xE09000, 0xE49400, 0xE89800, 0xECA000, 0xF0A800, 0xF4AC00, 0xFCB400,
    0x242424, 0x282828, 0x303030, 0x383434, 0x3C3C3C, 0x444040, 0x4C4848, 0x504C4C,
    0x585050, 0x605858, 0x685C5C, 0x6C6060, 0x746868, 0x7C6C6C, 0x807070, 0x887474,
    0x907C7C, 0x948080, 0x9C8484, 0xA48888, 0xA88C8C, 0xB09090, 0xB89494, 0xBC9898,
    0xC49C9C, 0xCCA0A0, 0xD4A4A4, 0xD8A8A8, 0xE0A8A8, 0xE8ACAC, 0xECB0B0, 0xF4B4B4,
    0xFC0000, 0xFC0000, 0xFC0404, 0xFC0808, 0xFC0808, 0xFC0C0C, 0xFC1010, 0xFC1414,
    0xFC1818, 0xFC1C1C, 0xFC2020, 0xFC2424, 0xFC2828, 0xFC2C2C, 0xFC3030, 0xFC3434,
    0xFC3838, 0xFC3C3C, 0xFC4040, 0xFC4444, 0xFC4848, 0xFC4C4C, 0xFC5050, 0xFC5454,
    0xFC5858, 0xFC5C5C, 0xFC6060, 0xFC6464, 0xFC6868, 0xFC6C6C, 0xFC7070, 0xFC7474,
    0x6C0024, 0x70002C, 0x740034, 0x780440, 0x7C0448, 0x800854, 0x880C5C, 0x8C0C68,
    0x901074, 0x941480, 0x981888, 0x9C1C94, 0xA420A0, 0xA024A8, 0xA028AC, 0x9C2CB0,
    0x9830B4, 0x9838B8, 0x983CC0, 0x9440C4, 0x9048C8, 0x904CCC, 0x9054D0, 0x8C58D4,
    0x8C60D8, 0x8C64E0, 0x8C6CE4, 0x8C70E8, 0x8C78EC, 0x8C80F0, 0x8C88F4, 0x9090FC,
    0x0000FC, 0x0004F8, 0x0410F8, 0x041CF8, 0x0824F4, 0x0C30F4, 0x0C3CF4, 0x1044F4,
    0x144CF0, 0x1454F0, 0x1860F0, 0x1C68EC, 0x2070EC, 0x2074EC, 0x247CEC, 0x2884E8,
    0x288CE8, 0x2C94E8, 0x3098E4, 0x30A0E4, 0x34A4E4, 0x38ACE4, 0x38B0E0, 0x3CB4E0,
    0x40BCE0, 0x40C0DC, 0x44C4DC, 0x44C8DC, 0x48CCD8, 0x4CD0D8, 0x4CD4D8, 0x50D8D8,
    0x102000, 0x102C00, 0x143800, 0x184400, 0x185000, 0x1C5800, 0x1C6400, 0x207000,
    0x207C00, 0x248800, 0x249000, 0x309800, 0x3C9C00, 0x44A000, 0x50A800, 0x58AC00,
    0x64B000, 0x6CB400, 0x78BC00, 0x84C000, 0x8CC400, 0x98CC00, 0xA0D000, 0xACD400,
    0xB4D800, 0xC0E000, 0xCCE400, 0xD4E800, 0xE0F000, 0xE8F400, 0xF4F800, 0xFCFC00,
};

const uint32_t Sg_AmigaRawDosGamePal[256] = {
    0x705000, 0x989898, 0xFCFCFC, 0x000000, 0x642000, 0xB84400, 0xFC6400, 0x605000,
    0x002000, 0x505000, 0xFC0000, 0x0000FC, 0x640020, 0x9898FC, 0x00DC00, 0xFCFC00,
    0x9CC800, 0xACACAC, 0xFCFCFC, 0x181818, 0x7C2400, 0xD04800, 0xFC8034, 0x84C800,
    0x003800, 0xC8C400, 0xFC6C6C, 0x787CFC, 0x7C0028, 0xBCBCFC, 0x00F000, 0xFCFCBC,
    0x6C8800, 0x747474, 0xD0D0D0, 0x000000, 0x481400, 0x782400, 0xB84000, 0x588400,
    0x000400, 0x848000, 0xCC0000, 0x0000CC, 0x3C0014, 0x787CCC, 0x00AC00, 0xCCC800,
    0x000000, 0x141414, 0x2C2C2C, 0x444444, 0x5C5C5C, 0x747474, 0x888888, 0xA0A0A0,
    0xB8B8B8, 0xD0D0D0, 0xE4E4E4, 0xFCFCFC, 0xFCD8BC, 0xFCB080, 0xFC8C40, 0xFC6400,
    0xE45800, 0xC84C00, 0xB04000, 0x983400, 0x802C00, 0x642000, 0x000000, 0x000070,
    0x000080, 0x000094, 0x0000A8, 0x2C2CC4, 0x6068E0, 0x9498E4, 0xC8B8A4, 0xB07800,
    0x442C00, 0x5C3800, 0xE8E8E8, 0xE0D4D4, 0xDCC4C4, 0xD8B8B8, 0xD4A8A8, 0xD09C9C,
    0xCC8C8C, 0xB07878, 0x906464, 0x745050, 0x583C3C, 0x382828, 0x1C1414, 0x000000,
    0xFCE8D0, 0xE8D0B0, 0xD4BC98, 0xC4A880, 0xB0946C, 0x947C5C, 0x7C644C, 0x605038,
    0x544434, 0x241C14, 0x543C08, 0xB89C78, 0x000000, 0x000000, 0x000000, 0x000000,
    0x0098FC, 0x505050, 0x707070, 0x202020, 0xF00000, 0xF07070, 0x808080, 0x909090,
    0xA0A0A0, 0xB0B0B0, 0xF0B0B0, 0xC0C0C0, 0xD0D0D0, 0xE0E0E0, 0xF0F0F0, 0xFC4C4C,
    0x342400, 0x5C5C5C, 0xC0C0C0, 0x000000, 0x280C00, 0x7C2C00, 0xC04800, 0x241C00,
    0x000000, 0x141400, 0xC00000, 0x0000C0, 0x000000, 0x7474C0, 0x00A000, 0xC0C000,
    0x6C8C00, 0x707070, 0xC0C0C0, 0x000000, 0x401000, 0x943000, 0xC06028, 0x5C8C00,
    0x000000, 0x8C8800, 0xC05050, 0x5C5CC0, 0x000000, 0x9090C0, 0x00B400, 0xC0C090,
    0x3C4C00, 0x383838, 0x949494, 0x000000, 0x0C0000, 0x3C1000, 0x7C2800, 0x2C4800,
    0x000000, 0x484400, 0x900000, 0x000090, 0x000000, 0x545490, 0x007000, 0x908C00,
    0x000000, 0x000000, 0x000000, 0x080808, 0x202020, 0x383838, 0x4C4C4C, 0x646464,
    0x7C7C7C, 0x949494, 0xA8A8A8, 0xC0C0C0, 0xC0A490, 0xC08460, 0xC06830, 0xC04800,
    0xA83C00, 0x8C3400, 0x742800, 0x5C1C00, 0x441400, 0x280C00, 0x000000, 0x000034,
    0x000044, 0x000058, 0x00006C, 0x1C1C88, 0x444CA4, 0x6C70A8, 0x8C8070, 0x744C00,
    0x080400, 0x201000, 0xACACAC, 0xA49898, 0xA09090, 0x9C8484, 0x987878, 0x946C6C,
    0x906060, 0x745050, 0x543838, 0x382424, 0x1C1010, 0x000000, 0x000000, 0x000000,
    0xC0B09C, 0xAC9880, 0x98846C, 0x887458, 0x746044, 0x584834, 0x403024, 0x241C14,
    0x18100C, 0x000000, 0x181000, 0x7C6850, 0x000000, 0x000000, 0x000000, 0x000000,
    0x0070C0, 0x141414, 0x343434, 0x000000, 0xB40000, 0xB45454, 0x444444, 0x545454,
    0x646464, 0x747474, 0xB48484, 0x848484, 0x949494, 0xA4A4A4, 0xB4B4B4, 0xC03838,
};

void Sg_AmigaPal12To24(const uint16_t *pal12, int32_t count, uint32_t *out)
{
    for (int32_t i = 0; i < count; i++) {
        uint32_t r = (pal12[i] >> 8) & 0xF, g = (pal12[i] >> 4) & 0xF, b = pal12[i] & 0xF;
        out[i] = (r << 20) | (g << 12) | (b << 4);
    }
}

/* ---- planar <-> chunky --------------------------------------------------- */

int32_t Sg_AmigaRawDimensions(uint32_t bytes, int32_t planes, int32_t *w, int32_t *h)
{
    static const int32_t sizes[2][2] = { { 320, 256 }, { 352, 272 } };
    for (int32_t i = 0; i < 2; i++) {
        if ((uint32_t)(sizes[i][0] / 8 * planes * sizes[i][1]) == bytes) {
            *w = sizes[i][0];
            *h = sizes[i][1];
            return 0;
        }
    }
    return -1;
}

int32_t Sg_AmigaRawDecode(const SgBuf *raw, int32_t w, int32_t h, int32_t planes,
                          SgImage *out, char *err)
{
    if (planes < 1 || planes > 8 || w <= 0 || h <= 0 || (w & 7)) {
        snprintf(err, 256, "amiga raw: bad geometry %dx%d, %d planes", (int)w, (int)h, (int)planes);
        return -1;
    }
    int32_t bpr = w / 8;                  /* bytes a plane row */
    uint32_t need = (uint32_t)bpr * (uint32_t)planes * (uint32_t)h;
    if (raw->size < need) {
        snprintf(err, 256, "amiga raw: %u bytes, %dx%d at %d planes needs %u",
                 (unsigned)raw->size, (int)w, (int)h, (int)planes, (unsigned)need);
        return -1;
    }
    if (SgImage_Init(out, w, h)) {
        snprintf(err, 256, "amiga raw: out of memory");
        return -1;
    }
    for (int32_t y = 0; y < h; y++) {
        const uint8_t *row = raw->data + (uint32_t)y * (uint32_t)bpr * (uint32_t)planes;
        uint8_t *dst = out->px + (size_t)y * (size_t)w;
        for (int32_t x = 0; x < w; x++) {
            int32_t bit = 7 - (x & 7);
            uint32_t v = 0;
            for (int32_t p = 0; p < planes; p++)
                v |= (uint32_t)((row[p * bpr + (x >> 3)] >> bit) & 1) << p;
            dst[x] = (uint8_t)v;
        }
    }
    return 0;
}

int32_t Sg_AmigaRawEncode(const SgImage *img, int32_t planes, SgBuf *out, char *err)
{
    out->data = NULL;
    out->size = 0;
    if (img->w <= 0 || img->h <= 0 || (img->w & 7)) {
        snprintf(err, 256, "amiga raw: width %d is not a multiple of 8", (int)img->w);
        return -1;
    }
    uint32_t n = (uint32_t)img->w * (uint32_t)img->h;
    if (planes == 0) {                    /* -bitplanes=auto */
        uint8_t highest = 0;
        for (uint32_t i = 0; i < n; i++)
            if (img->px[i] > highest)
                highest = img->px[i];
        planes = highest <= 15 ? 4 : 8;
    }
    if (planes < 1 || planes > 8) {
        snprintf(err, 256, "amiga raw: %d planes", (int)planes);
        return -1;
    }
    uint32_t maxIndex = (1u << planes) - 1;
    for (uint32_t i = 0; i < n; i++) {
        if (img->px[i] > maxIndex) {
            snprintf(err, 256, "amiga raw: pixel index %u at (%d,%d) does not fit %d planes",
                     (unsigned)img->px[i], (int)(i % (uint32_t)img->w), (int)(i / (uint32_t)img->w),
                     (int)planes);
            return -1;
        }
    }
    int32_t bpr = img->w / 8;
    out->size = (uint32_t)bpr * (uint32_t)planes * (uint32_t)img->h;
    out->data = malloc(out->size);
    if (!out->data) {
        out->size = 0;
        snprintf(err, 256, "amiga raw: out of memory");
        return -1;
    }
    uint8_t *d = out->data;
    for (int32_t y = 0; y < img->h; y++) {
        const uint8_t *src = img->px + (size_t)y * (size_t)img->w;
        for (int32_t p = 0; p < planes; p++) {
            for (int32_t b = 0; b < bpr; b++) {
                uint8_t v = 0;
                for (int32_t bit = 0; bit < 8; bit++)
                    v = (uint8_t)((v << 1) | ((src[b * 8 + bit] >> p) & 1));
                *d++ = v;
            }
        }
    }
    return 0;
}

/* ---- the 256-colour conversion ------------------------------------------ */

/* AmigaPalette.GetNearestColor: Euclidean RGB, the first of equals wins. The
 * squared distance orders exactly as SwosGfx's sqrt does. */
static int32_t NearestColor(uint32_t c, const uint32_t *pal, int32_t count)
{
    int32_t best = -1;
    int32_t bestD = 0x7FFFFFFF;
    int32_t r = (int32_t)((c >> 16) & 0xFF), g = (int32_t)((c >> 8) & 0xFF), b = (int32_t)(c & 0xFF);
    for (int32_t i = 0; i < count; i++) {
        int32_t dr = r - (int32_t)((pal[i] >> 16) & 0xFF);
        int32_t dg = g - (int32_t)((pal[i] >> 8) & 0xFF);
        int32_t db = b - (int32_t)(pal[i] & 0xFF);
        int32_t d = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

void Sg_AmigaRemapToPalette(SgImage *img, const uint32_t *oldPal, int32_t oldCount,
                            const uint32_t *newPal, int32_t newCount)
{
    uint8_t map[256];
    for (int32_t i = 0; i < 256; i++)
        map[i] = (uint8_t)i;
    for (int32_t i = 0; i < oldCount && i < 256; i++) {
        uint32_t c = oldPal[i] & 0xFFFFFF;
        if (i < newCount && c == (newPal[i] & 0xFFFFFF))
            continue;
        int32_t n = NearestColor(c, newPal, newCount);
        if (n >= 0)
            map[i] = (uint8_t)n;
    }
    uint32_t n = (uint32_t)img->w * (uint32_t)img->h;
    for (uint32_t i = 0; i < n; i++)
        if (img->px[i] != 0 && img->px[i] < oldCount)   /* 0 is transparent: never moved */
            img->px[i] = map[img->px[i]];
    SgImage_SetPaletteArgb(img, newPal, newCount);
}

static int32_t StartsWithNoCase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char a = *s, b = *prefix;
        if (a >= 'a' && a <= 'z') a = (char)(a - 32);
        if (a != b)
            return 0;
    }
    return 1;
}

static int32_t ContainsNoCase(const char *s, const char *word)
{
    for (; *s; s++)
        if (StartsWithNoCase(s, word))
            return 1;
    return 0;
}

int32_t Sg_AmigaPictureTo256(const char *name, const SgBuf *raw, SgImage *out, char *err)
{
    const uint16_t *pal12;
    const uint32_t *newPal;

    /* Program.cs GetPaletteRequired, in its order (with -colors=256). */
    if (StartsWithNoCase(name, "LOADER")) {
        const char *idx = name + 6;
        if (strcmp(idx, "00") == 0)
            pal12 = Sg_AmigaPal12TitleLogo;
        else if (strcmp(idx, "1") == 0)
            pal12 = Sg_AmigaPal12TitleScreen;
        else
            pal12 = Sg_AmigaPal12TitleText;
        newPal = Sg_AmigaRawDosMenuPal;
    } else if (StartsWithNoCase(name, "MENU") || StartsWithNoCase(name, "SOCCER") ||
               ContainsNoCase(name, "DISK")) {
        pal12 = Sg_AmigaPal12Menu;
        newPal = Sg_AmigaRawDosMenuPal;
    } else if (StartsWithNoCase(name, "CJC") || StartsWithNoCase(name, "CHARSET")) {
        pal12 = Sg_AmigaPal12Menu;
        newPal = Sg_AmigaRawDosGamePal;
    } else {
        snprintf(err, 256, "amiga raw: no palette known for %s", name);
        return -1;
    }

    SgBuf un;
    if (Sg_RncUnpack(raw, &un, err))
        return -1;
    int32_t w, h;
    if (Sg_AmigaRawDimensions(un.size, 4, &w, &h)) {
        snprintf(err, 256, "amiga raw: %s is %u bytes, not a 4-plane 320x256 or 352x272 picture",
                 name, (unsigned)un.size);
        Sg_BufFree(&un);
        return -1;
    }
    int32_t rc = Sg_AmigaRawDecode(&un, w, h, 4, out, err);
    Sg_BufFree(&un);
    if (rc)
        return -1;

    uint32_t oldPal[16];
    Sg_AmigaPal12To24(pal12, 16, oldPal);
    Sg_AmigaRemapToPalette(out, oldPal, 16, newPal, 256);
    return 0;
}
