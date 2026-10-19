/*
 * pal_file.h -- palette files for paint programs.
 *
 * A port of SwosGfx's PalFile.cs, the writing half: Photoshop .act, the
 * Microsoft RIFF .pal, Paint Shop Pro's JASC .pal, GIMP .gpl and Paint.NET .txt.
 * PalFile.cs's readers (TryReadPalette) are not ported: SwosGfx never calls them.
 *
 * The text formats end their lines with "\r\n" on every host: SwosGfx is a
 * Windows tool (File.WriteAllLines writes Environment.NewLine), and its files
 * are the reference these must equal byte for byte.
 */
#ifndef SWOSGFX_PAL_FILE_H
#define SWOSGFX_PAL_FILE_H

#include <stdint.h>

typedef enum {
    SG_PAL_FORMAT_ACT,
    SG_PAL_FORMAT_MSPAL,
    SG_PAL_FORMAT_JASC,
    SG_PAL_FORMAT_GIMP,
    SG_PAL_FORMAT_PAINTNET,
    SG_PAL_FORMAT_COUNT
} SgPalFormat;

/* The -pal-format= names, in enum order: act, mspal, jasc, gimp, paintnet. */
extern const char *const Sg_PalFormatNames[SG_PAL_FORMAT_COUNT];

/* The extension SwosGfx gives each format (AmigaPalette.PaletteToPalette):
 * ".act", ".pal", ".pal", ".gpl", ".txt". NULL for a format out of range. */
const char *Sg_PalFormatExtension(SgPalFormat format);

/* PalFile.WritePalette: write `count` colours (0xAARRGGBB; only RGB is written,
 * except Paint.NET, which writes the whole ARGB) to `fileName`, creating its
 * folders. `transparentIndex` is -1 for none (only .act records it). The GIMP
 * and Paint.NET files name the palette after the file, without its extension.
 * 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_PalFileWrite(const char *fileName, const uint32_t *argb, int32_t count,
                        int32_t transparentIndex, SgPalFormat format, char *err);

#endif
