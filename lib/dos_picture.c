/*
 * dos_picture.c -- the PC game's .256 pictures, and their enlargement to the
 * Amiga's full screen.
 *
 * A .256 file is exactly 64,768 bytes, nothing compressed:
 *   +0      320 x 200 palette indices, top row first
 *   +64000  256 palette entries, R G B, each 0..63 (VGA DAC values)
 *
 * This is a port of SwosGfx (DosPicture.cs, DosPictureScale.cs), and it must
 * give the same pixels, so it repeats SwosGfx's arithmetic exactly:
 *
 *  - The palette is the 6-bit values times 4 (so 0..252, never 255).
 *  - Loading picks a "text colour": the colour with the greatest
 *    (int)sqrt(r*r + g*g + b*b), the LAST one on a tie. If that is under 381,
 *    the least-used index (the FIRST one on a tie) is replaced by white
 *    (255,255,255) in the palette. That changed palette is the one the
 *    enlargement interpolates and searches, and the one written to the BMP.
 *  - The enlargement maps each output pixel's centre back to the source:
 *    s = (d + 0.5) * src / dst - 0.5 in doubles, floor for the left/top
 *    neighbour, the fraction as the weight, neighbours clamped to the edge;
 *    each channel is lerped across then down, in doubles, and rounded half to
 *    EVEN (.NET's Math.Round); the colour is then the palette entry with the
 *    least squared RGB distance, the lowest index on a tie.
 *
 * Doubles must be IEEE binary64 evaluated at that precision (SSE2, ARM, any
 * x86-64 compiler); an x87 build with extended-precision intermediates could
 * round a weight differently.
 */
#include "dos_picture.h"
#include "sg_dir.h"
#include "sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIXEL_BYTES   (SG_DOS_PICTURE_W * SG_DOS_PICTURE_H)
#define FILE_BYTES    (PIXEL_BYTES + 256 * 3)
#define TEXT_DELTA    381

/* floor() and (int)Math.Sqrt without libm (the Makefile links none). Both are
 * exact: the values are small integers / doubles well inside int32 range, and
 * a correctly rounded sqrt of an integer under 2^26 never rounds up across an
 * integer, so truncating it is the integer square root. */
static int32_t floori(double v)
{
    int32_t t = (int32_t)v;
    return (double)t > v ? t - 1 : t;
}

static int32_t isqrt(int32_t n)
{
    int32_t r = 0;
    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

/* The text colour, as DosPicture.ComputeTextColorIndex: may whiten an entry. */
static int32_t text_color(SgImage *img)
{
    int32_t i, maxColor = 0, maxDelta = 0;
    for (i = 0; i < 256; i++) {
        int32_t r = img->pal[i][0], g = img->pal[i][1], b = img->pal[i][2];
        int32_t delta = isqrt(r * r + g * g + b * b);
        if (delta >= maxDelta) {
            maxDelta = delta;
            maxColor = i;
        }
    }
    if (maxDelta < TEXT_DELTA) {
        int32_t usage[256];
        int32_t n = img->w * img->h, minUsed = 0;
        memset(usage, 0, sizeof usage);
        for (i = 0; i < n; i++)
            usage[img->px[i]]++;
        for (i = 1; i < 256; i++)
            if (usage[i] < usage[minUsed])
                minUsed = i;
        img->pal[minUsed][0] = img->pal[minUsed][1] = img->pal[minUsed][2] = 255;
        maxColor = minUsed;
    }
    return maxColor;
}

int32_t Sg_DosPictureLoadEx(const char *path, SgImage *out, int32_t *textColor, char *err)
{
    SgBuf b;
    int32_t i, tc;

    memset(out, 0, sizeof *out);
    if (Sg_ReadFile(path, &b, err) != 0)
        return -1;
    if (b.size != FILE_BYTES) {
        snprintf(err, 256, "%s is not a .256 picture (%u bytes, not %d)", path,
                 (unsigned)b.size, FILE_BYTES);
        Sg_BufFree(&b);
        return -1;
    }
    if (SgImage_Init(out, SG_DOS_PICTURE_W, SG_DOS_PICTURE_H) != 0) {
        snprintf(err, 256, "out of memory");
        Sg_BufFree(&b);
        return -1;
    }
    memcpy(out->px, b.data, PIXEL_BYTES);
    for (i = 0; i < 256; i++) {
        out->pal[i][0] = (uint8_t)(b.data[PIXEL_BYTES + i * 3 + 0] * 4);
        out->pal[i][1] = (uint8_t)(b.data[PIXEL_BYTES + i * 3 + 1] * 4);
        out->pal[i][2] = (uint8_t)(b.data[PIXEL_BYTES + i * 3 + 2] * 4);
    }
    Sg_BufFree(&b);
    tc = text_color(out);
    if (textColor)
        *textColor = tc;
    return 0;
}

int32_t Sg_DosPictureLoad(const char *path, SgImage *out, char *err)
{
    return Sg_DosPictureLoadEx(path, out, NULL, err);
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static double bilerp(int32_t c00, int32_t c01, int32_t c10, int32_t c11, double fx, double fy)
{
    double top = c00 + (c01 - c00) * fx;
    double bot = c10 + (c11 - c10) * fx;
    return top + (bot - top) * fy;
}

/* Math.Round(double): to nearest, a half to the even neighbour. */
static int32_t round_even(double v)
{
    int32_t f = floori(v);
    double d = v - f;
    if (d > 0.5 || (d == 0.5 && (f & 1)))
        f++;
    return f;
}

static int32_t nearest(const SgImage *img, int32_t r, int32_t g, int32_t b)
{
    int32_t i, best = 0;
    int64_t bestD = INT64_MAX;
    for (i = 0; i < 256; i++) {
        int64_t dr = r - img->pal[i][0], dg = g - img->pal[i][1], db = b - img->pal[i][2];
        int64_t d = dr * dr + dg * dg + db * db;
        if (d < bestD) {
            bestD = d;
            best = i;
            if (d == 0)
                break;
        }
    }
    return best;
}

int32_t Sg_DosPictureScale(const SgImage *in, int32_t w, int32_t h, SgImage *out, char *err)
{
    int32_t x, y, srcW = in->w, srcH = in->h;

    if (!in->px || srcW <= 0 || srcH <= 0) {
        snprintf(err, 256, "no picture to scale");
        return -1;
    }
    if (SgImage_Init(out, w, h) != 0) {
        snprintf(err, 256, "cannot make a %dx%d picture", (int)w, (int)h);
        return -1;
    }
    memcpy(out->pal, in->pal, sizeof out->pal);

    for (y = 0; y < h; y++) {
        double syf = (y + 0.5) * srcH / h - 0.5;
        int32_t y0 = floori(syf);
        double fy = syf - y0;
        int32_t y0c = clampi(y0, 0, srcH - 1);
        int32_t y1c = clampi(y0 + 1, 0, srcH - 1);

        for (x = 0; x < w; x++) {
            double sxf = (x + 0.5) * srcW / w - 0.5;
            int32_t x0 = floori(sxf);
            double fx = sxf - x0;
            int32_t x0c = clampi(x0, 0, srcW - 1);
            int32_t x1c = clampi(x0 + 1, 0, srcW - 1);
            const uint8_t *c00 = in->pal[in->px[y0c * srcW + x0c]];
            const uint8_t *c01 = in->pal[in->px[y0c * srcW + x1c]];
            const uint8_t *c10 = in->pal[in->px[y1c * srcW + x0c]];
            const uint8_t *c11 = in->pal[in->px[y1c * srcW + x1c]];
            int32_t r = round_even(bilerp(c00[0], c01[0], c10[0], c11[0], fx, fy));
            int32_t g = round_even(bilerp(c00[1], c01[1], c10[1], c11[1], fx, fy));
            int32_t b = round_even(bilerp(c00[2], c01[2], c10[2], c11[2], fx, fy));
            out->px[y * w + x] = (uint8_t)nearest(in, r, g, b);
        }
    }
    return 0;
}

int32_t Sg_DosPictureScaleToAmiga(const SgImage *in, SgImage *out, char *err)
{
    return Sg_DosPictureScale(in, SG_AMIGA_SCREEN_W, SG_AMIGA_SCREEN_H, out, err);
}

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

int32_t Sg_DosPictureWriteBmp(const SgImage *img, const char *path, char *err)
{
    uint32_t rowSize = (uint32_t)((img->w + 3) / 4) * 4;
    uint32_t imageSize = rowSize * (uint32_t)img->h;
    uint32_t headerSize = 14 + 40 + 256 * 4;
    uint32_t fileSize = headerSize + imageSize, i;
    int32_t y, rc;
    uint8_t *f = (uint8_t *)calloc(fileSize, 1);

    if (!f) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    put16(f + 0, 0x4D42);
    put32(f + 2, fileSize);
    put32(f + 10, headerSize);
    put32(f + 14, 40);
    put32(f + 18, (uint32_t)img->w);
    put32(f + 22, (uint32_t)img->h);      /* positive: bottom-up */
    put16(f + 26, 1);
    put16(f + 28, 8);
    put32(f + 34, imageSize);             /* compression, ppm, ClrUsed, ClrImportant: 0 */
    for (i = 0; i < 256; i++) {
        f[54 + i * 4 + 0] = img->pal[i][2];
        f[54 + i * 4 + 1] = img->pal[i][1];
        f[54 + i * 4 + 2] = img->pal[i][0];
    }
    for (y = 0; y < img->h; y++)
        memcpy(f + headerSize + (uint32_t)(img->h - 1 - y) * rowSize, img->px + y * img->w,
               (size_t)img->w);
    rc = Sg_WriteFile(path, f, fileSize, err);
    free(f);
    return rc;
}

/* ---- -picture --------------------------------------------------------------- */

/* DosPicture.Load's error, by the names of SwosGfx's DosPictureError. */
static int32_t LoadNamed(const char *path, const char *shown, SgImage *pic, char *err)
{
    const char *why = NULL;
    SgBuf b;

    if (!Sg_IsFile(path))
        why = "File";
    else if (Sg_ReadFile(path, &b, err) != 0)
        why = "Reading";
    else {
        if (b.size != FILE_BYTES)
            why = "InvalidSize";
        Sg_BufFree(&b);
    }
    if (!why && Sg_DosPictureLoad(path, pic, err) != 0)
        why = "Reading";
    if (why) {
        snprintf(err, 256, "Failed to load DOS picture '%.200s': %s", shown, why);
        return -1;
    }
    return 0;
}

static int32_t IsRooted(const char *p)
{
    return p[0] == '/' || p[0] == '\\' ||
           (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':');
}

int32_t Sg_DosPictureRun(const char *dosDir, const char *inPic, const char *out,
                         int32_t scaleW, int32_t scaleH, SgLogFn log, void *user, char *err)
{
    char path[1024], outBmp[1024], name[300], line[800];
    SgImage pic;
    int32_t rc;

    if (!dosDir)
        dosDir = "";
    memset(&pic, 0, sizeof pic);

    if (!inPic) {
        char **names = NULL;
        int32_t n = 0;

        if (!out || !out[0])
            out = ".";
        if (Sg_ListDir(dosDir[0] ? dosDir : ".", &names, &n) != 0) {
            snprintf(err, 256, "cannot read the folder %.200s", dosDir);
            return -1;
        }
        rc = 0;
        for (int32_t i = 0; i < n && rc == 0; i++) {
            if (!Sg_EndsWithNoCase(names[i], ".256"))
                continue;
            Sg_PathJoin(path, sizeof path, dosDir, names[i]);
            rc = LoadNamed(path, path, &pic, err);
            if (rc == 0) {
                Sg_PathChangeExt(name, sizeof name, names[i], ".bmp");
                Sg_PathJoin(outBmp, sizeof outBmp, out, name);
                rc = Sg_DosPictureWriteBmp(&pic, outBmp, err);
                SgImage_Free(&pic);
            }
        }
        Sg_ListDirFree(names, n);
        return rc;
    }

    /* A bare name like SWTITLE.256 loads from the PC game's folder; otherwise
     * the path as given (absolute, or from the current folder). */
    if (IsRooted(inPic))
        snprintf(path, sizeof path, "%s", inPic);
    else if (!dosDir[0] || Sg_FindNoCase(dosDir, inPic, path, sizeof path) != 0 || !Sg_IsFile(path))
        snprintf(path, sizeof path, "%s", inPic);
    if (LoadNamed(path, inPic, &pic, err) != 0)
        return -1;

    /* A folder, or anything not named .bmp, gets <inputname>.bmp inside it. */
    if (!out || !out[0])
        out = ".";
    if (Sg_IsDir(out) || !Sg_EndsWithNoCase(out, ".bmp")) {
        if (Sg_MakeDirs(out) != 0) {
            snprintf(err, 256, "cannot create the folder %.200s", out);
            SgImage_Free(&pic);
            return -1;
        }
        Sg_PathChangeExt(name, sizeof name, inPic, ".bmp");
        Sg_PathJoin(outBmp, sizeof outBmp, out, name);
    } else {
        snprintf(outBmp, sizeof outBmp, "%s", out);
    }

    if (scaleW > 0 && scaleH > 0) {
        SgImage big;
        rc = Sg_DosPictureScale(&pic, scaleW, scaleH, &big, err);
        if (rc == 0) {
            rc = Sg_DosPictureWriteBmp(&big, outBmp, err);
            SgImage_Free(&big);
        }
        if (rc == 0 && log) {
            snprintf(line, sizeof line, "Scaled '%s' %dx%d -> '%s' %dx%d (bilinear, 8bpp).",
                     Sg_PathFileName(inPic), SG_DOS_PICTURE_W, SG_DOS_PICTURE_H,
                     Sg_PathFileName(outBmp), (int)scaleW, (int)scaleH);
            log(user, line);
        }
    } else {
        rc = Sg_DosPictureWriteBmp(&pic, outBmp, err);
    }
    SgImage_Free(&pic);
    return rc;
}
