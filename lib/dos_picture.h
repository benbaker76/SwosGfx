/*
 * dos_picture.h -- the PC game's full-screen pictures (.256) and their
 * enlargement to the Amiga's 352x272 screen (SWTITLE.256 -> MENUBG,
 * STAD.256 -> MENUBG2). A C port of SwosGfx's DosPicture / DosPictureScale.
 */
#ifndef SWOSGFX_DOS_PICTURE_H
#define SWOSGFX_DOS_PICTURE_H

#include <stdint.h>
#include "sg_image.h"
#include "sg_log.h"

#define SG_DOS_PICTURE_W   320
#define SG_DOS_PICTURE_H   200
#define SG_AMIGA_SCREEN_W  352
#define SG_AMIGA_SCREEN_H  272

/* Load a .256 picture: 320x200 indices and its palette, the palette as SwosGfx
 * keeps it (6-bit VGA values x 4, and the least-used colour made white when no
 * colour is bright enough to write on -- see dos_picture.c). If textColor is
 * not NULL it gets that writing colour's index. `out` is initialised here;
 * SgImage_Free it. 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_DosPictureLoadEx(const char *path, SgImage *out, int32_t *textColor, char *err);
int32_t Sg_DosPictureLoad(const char *path, SgImage *out, char *err);

/* Bilinear-enlarge (or shrink) `in` to w x h, each pixel put back on in's own
 * palette by nearest colour: SwosGfx's -picture -scale=WxH, to the bit. The
 * palette is copied to `out`, which is initialised here. 0 on success. */
int32_t Sg_DosPictureScale(const SgImage *in, int32_t w, int32_t h, SgImage *out, char *err);

/* The same to 352x272 (-scale with no size): MENUBG / MENUBG2. */
int32_t Sg_DosPictureScaleToAmiga(const SgImage *in, SgImage *out, char *err);

/* Write an 8bpp BMP exactly as SwosGfx's -picture -scale writes it
 * (DosPictureScale.SaveIndexedBmp). It differs from SgImage_WriteBmp in one
 * header field only: biClrUsed is 0 there, 256 in SwosGfx's other BMPs.
 * Pixels and palette are the same either way. 0 on success. */
int32_t Sg_DosPictureWriteBmp(const SgImage *img, const char *path, char *err);

/* SwosGfx -dos -picture -output=bmp [-scale[=WxH]] (Program.cs RunDos):
 *
 * inPic set: one picture. It is looked for as <dosDir>/<inPic> first (any case),
 *   then as the path given. `out` is a .bmp file, or -- when it is an existing
 *   folder or does not end in ".bmp" -- a folder (made if need be) that gets
 *   <inPic's name>.bmp. With scaleW > 0 the picture is enlarged to scaleW x
 *   scaleH (Sg_DosPictureScale; -scale alone is 352x272) and SwosGfx's line
 *   "Scaled 'IN.256' 320x200 -> 'OUT.bmp' WxH (bilinear, 8bpp)." goes to `log`.
 * inPic NULL: every *.256 in dosDir (any case) to <out>/<NAME>.bmp, `out` NULL
 *   meaning the current folder. Not scaled, whatever scaleW says: SwosGfx's
 *   batch has no -scale. (SwosGfx's own command line never reaches its batch --
 *   it insists on two file names -- but the code is there, and this is it.)
 *
 * Every BMP is Sg_DosPictureWriteBmp's, SwosGfx's bytes for both paths. A file
 * that is not a picture fails as SwosGfx does: "Failed to load DOS picture
 * 'IN.256': File" (not found), "...: Reading", "...: InvalidSize".
 * 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_DosPictureRun(const char *dosDir, const char *inPic, const char *out,
                         int32_t scaleW, int32_t scaleH, SgLogFn log, void *user, char *err);

#endif
