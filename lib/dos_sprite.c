/*
 * dos_sprite.c -- the PC game's sprites: SPRITE.DAT and the *.DAT containers.
 *
 * A container record (packed, little-endian, 24 bytes, then the pixels):
 *    0  uint32 spr_data      a pointer the game fixes up; ignored
 *    4  int16  size          0 on disk
 *    6  int16  dat_file      0 on disk
 *    8  uint8  changed       0 on disk
 *    9  int8   unk1
 *   10  int16  width         pixels
 *   12  int16  nlines        height
 *   14  int16  wquads        16-pixel units per line
 *   16  int16  center_x
 *   18  int16  center_y
 *   20  uint8  unk4
 *   21  uint8  nlines_div4
 *   22  int16  ordinal
 *   24  nlines * wquads * 8 bytes of chained pixels
 *
 * Ported from SwosGfx DosSprite.cs (ReadSpriteFromDatBuffer, DecodeSpriteChunky)
 * and DosSpriteCodec.cs (ChainSprite, decode_quad): the rcl chain is emulated
 * bit for bit, carry flag included, rather than re-derived.
 */
#include "dos_sprite.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_SIZE      24
#define LINE_BUFFER_SIZE (256 - 8)   /* the PC tool's temp line: a longer one is refused */

const SgDosDatSpec Sg_DosDatSpecs[SG_DOS_NUM_DAT_FILES] = {
    { "CHARSET.DAT", 227,    0 },
    { "SCORE.DAT",   114,  227 },
    { "TEAM1.DAT",   303,  341 },
    { "TEAM3.DAT",   303,  644 },
    { "GOAL1.DAT",   116,  947 },
    { "GOAL1.DAT",   116, 1063 },
    { "BENCH.DAT",   155, 1179 },
};

static int16_t Le16s(const uint8_t *p) { return (int16_t)Sg_Le16(p); }

int32_t Sg_DosDatLoad(SgDosDat *d, const char *dosDir, const char *name, int32_t count, char *err)
{
    char path[1024];
    uint32_t ofs = 0;

    memset(d, 0, sizeof *d);
    if (Sg_FindNoCase(dosDir, name, path, sizeof path) != 0) {
        snprintf(err, 256, "%s not found in %s", name, dosDir);
        return -1;
    }
    if (Sg_ReadFile(path, &d->buf, err) != 0)
        return -1;
    d->spr = calloc((size_t)(count > 0 ? count : 1), sizeof *d->spr);
    if (!d->spr) {
        snprintf(err, 256, "out of memory reading %s", name);
        Sg_DosDatFree(d);
        return -1;
    }
    d->count = count;

    for (int32_t j = 0; j < count; j++) {
        const uint8_t *h = d->buf.data + ofs;
        SgDosSprite *s = &d->spr[j];
        int32_t bytes;

        if (ofs + RECORD_SIZE > d->buf.size) {
            snprintf(err, 256, "%s: ends inside the header of sprite %d", name, (int)j);
            Sg_DosDatFree(d);
            return -1;
        }
        s->width      = Le16s(h + 10);
        s->height     = Le16s(h + 12);
        s->wquads     = Le16s(h + 14);
        s->centerX    = Le16s(h + 16);
        s->centerY    = Le16s(h + 18);
        s->unk4       = h[20];
        s->heightDiv4 = h[21];
        s->ordinal    = Le16s(h + 22);
        ofs += RECORD_SIZE;

        if (s->height < 0 || s->wquads < 0) {
            snprintf(err, 256, "%s: sprite %d has a negative size", name, (int)j);
            Sg_DosDatFree(d);
            return -1;
        }
        bytes = (int32_t)s->height * s->wquads * 8;
        if ((uint32_t)bytes > d->buf.size - ofs) {
            snprintf(err, 256, "%s: sprite %d runs past the end of the file", name, (int)j);
            Sg_DosDatFree(d);
            return -1;
        }
        s->data = d->buf.data + ofs;
        s->size = (uint32_t)bytes;
        ofs += (uint32_t)bytes;
    }
    /* Bytes after the last record are ignored, as SwosGfx ignores them. */
    return 0;
}

void Sg_DosDatFree(SgDosDat *d)
{
    Sg_BufFree(&d->buf);
    free(d->spr);
    d->spr = NULL;
    d->count = 0;
}

int32_t Sg_DosSpritesLoad(SgDosSprites *s, const char *dosDir, char *err)
{
    char path[1024];
    SgBuf idx;

    memset(s, 0, sizeof *s);
    if (Sg_FindNoCase(dosDir, "SPRITE.DAT", path, sizeof path) != 0) {
        snprintf(err, 256, "SPRITE.DAT not found in %s", dosDir);
        return -1;
    }
    if (Sg_ReadFile(path, &idx, err) != 0)
        return -1;
    if (idx.size != SG_DOS_SPRITE_DAT_SIZE) {
        snprintf(err, 256, "SPRITE.DAT is %u bytes, expected %d", (unsigned)idx.size, SG_DOS_SPRITE_DAT_SIZE);
        Sg_BufFree(&idx);
        return -1;
    }
    Sg_BufFree(&idx);

    for (int32_t i = 0; i < SG_DOS_NUM_DAT_FILES; i++) {
        const SgDosDatSpec *spec = &Sg_DosDatSpecs[i];
        if (Sg_DosDatLoad(&s->dat[i], dosDir, spec->name, spec->entries, err) != 0) {
            Sg_DosSpritesFree(s);
            return -1;
        }
        for (int32_t j = 0; j < spec->entries; j++)
            s->spr[spec->start + j] = &s->dat[i].spr[j];
    }
    return 0;
}

void Sg_DosSpritesFree(SgDosSprites *s)
{
    for (int32_t i = 0; i < SG_DOS_NUM_DAT_FILES; i++)
        Sg_DosDatFree(&s->dat[i]);
    memset((void *)s->spr, 0, sizeof s->spr);
}

/* ---- ChainSprite: the x86 rcl sequence, emulated ---------------------- */

static void Rcl8(uint8_t *v, int32_t *cf)
{
    int32_t out = (*v >> 7) & 1;
    *v = (uint8_t)((*v << 1) | (*cf & 1));
    *cf = out;
}

/* rcl bl,1; rcl al,1; rcl bh,1; rcl al,1; rcl cl,1; rcl al,1; rcl ch,1; rcl al,1 */
static void GetBitsStep(uint8_t *al, uint8_t *bl, uint8_t *bh, uint8_t *cl, uint8_t *ch, int32_t *cf)
{
    Rcl8(bl, cf); Rcl8(al, cf);
    Rcl8(bh, cf); Rcl8(al, cf);
    Rcl8(cl, cf); Rcl8(al, cf);
    Rcl8(ch, cf); Rcl8(al, cf);
}

/* 8 bytes of chain data -> 8 bytes of linear 4bpp (16 pixels). */
static void DecodeQuad(const uint8_t chain[8], uint8_t *dst)
{
    uint8_t bl, bh, cl, ch, al;
    int32_t cf = 0;

    ch = chain[0]; cl = chain[2]; bh = chain[4]; bl = chain[6]; al = 0;
    for (int32_t i = 0; i < 4; i++) {
        GetBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        GetBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        *dst++ = al;
    }
    /* second half: the carry carries over */
    ch = chain[1]; cl = chain[3]; bh = chain[5]; bl = chain[7]; al = 0;
    for (int32_t i = 0; i < 4; i++) {
        GetBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        GetBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        *dst++ = al;
    }
}

int32_t Sg_DosSpriteDecode(const SgDosSprite *spr, uint8_t *out)
{
    int32_t w = spr->width, h = spr->height, wq = spr->wquads;
    int32_t bytesPerLine = wq * 8, units4 = wq * 2;

    if (w <= 0 || h <= 0)
        return 0;
    /* The packed pixels a line reads: (w-1)/2 + 1 bytes, which on the last
     * line must still be inside the record (on others it may spill into the
     * next line, as it does in SwosGfx). */
    if (wq <= 0 || bytesPerLine > LINE_BUFFER_SIZE ||
        (uint32_t)((h - 1) * bytesPerLine + (w - 1) / 2 + 1) > spr->size)
        return -1;

    /* The C# de-chains a copy of the whole sprite and then reads it, so a
     * spill into the next line reads that line DE-chained. Do the same. */
    {
        uint8_t *lin = malloc(spr->size);
        if (!lin)
            return -1;
        for (int32_t y = 0; y < h; y++) {
            const uint8_t *src = spr->data + y * bytesPerLine;
            for (int32_t q = 0; q < wq; q++) {
                const uint8_t *p = src + q * 2;
                uint8_t chain[8];
                chain[0] = p[0];          chain[1] = p[1];
                chain[2] = p[units4];     chain[3] = p[units4 + 1];
                chain[4] = p[units4 * 2]; chain[5] = p[units4 * 2 + 1];
                chain[6] = p[units4 * 3]; chain[7] = p[units4 * 3 + 1];
                DecodeQuad(chain, lin + y * bytesPerLine + q * 8);
            }
        }
        for (int32_t y = 0; y < h; y++) {
            const uint8_t *src = lin + y * bytesPerLine;
            uint8_t *dst = out + y * w;
            for (int32_t x = 0; x < w; x++) {
                uint8_t b = src[x >> 1];
                dst[x] = (uint8_t)((x & 1) ? (b & 0x0F) : (b >> 4));
            }
        }
        free(lin);
    }
    return 0;
}

/* ---- palettes (SwosGfx DosPalette.cs) --------------------------------- */

const uint32_t Sg_DosSpritePalMenu[256] = {
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

const uint32_t Sg_DosSpritePalGame[256] = {
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
