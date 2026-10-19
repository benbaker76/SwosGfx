/*
 * dos_pitch_render.h -- the PC game's pitches drawn whole: SwosGfx's -dos pitch
 * mode (-output=bmp | -output=tmj).
 *
 * A port of SwosGfx's DosPitch.cs. A pitch is the 42 x 55 grid of 16x16 cells
 * of its pointer table (672 x 880 pixels), each cell the pattern its pointer
 * names, in the game palette with the nine entries of a pitch type replaced
 * (Sg_DosGetPitchPalette). Not to be confused with dos_pitch.h, which turns the
 * same files into OpenSWOS's AGA pitch maps.
 *
 * The Tiled export (SavePitchAsTiled) writes <name>.tmj and the tileset
 * <name>.bmp beside it. Tiles are deduplicated on PIXELS, not on pattern index,
 * in first-use order (row by row), as Tiled2Bin -slice -norepeat does. When the
 * pitch has fans the crowd moves onto two layers of its own (the v2 pitch
 * format): the fan cells of the stands (rows 1-4 and 50-54; row 0 is the fan
 * POINTER TABLE, never drawn) are 0 in the "pitch" layer and instead hold the
 * sitting tile in "crowd-sit" (visible) and the standing one in "crowd-stand"
 * (hidden). A fan is a pattern index in 1..22 (the engine's MAX_FAN_TILE_INDEX),
 * odd sitting, even standing.
 */
#ifndef SWOSGFX_DOS_PITCH_RENDER_H
#define SWOSGFX_DOS_PITCH_RENDER_H

#include <stdint.h>

#include "dos_pitch_pattern.h"
#include "sg_log.h"

#define SG_DOS_PITCH_W        672   /* Width  (PITCH_W) */
#define SG_DOS_PITCH_H        880   /* Height (PITCH_H) */
#define SG_DOS_PITCH_COLUMNS  42
#define SG_DOS_PITCH_ROWS     55

#define SG_DOS_PITCH_DEFAULT_FIRST_FAN  1
#define SG_DOS_PITCH_DEFAULT_LAST_FAN   22

/* PitchType: Frozen, Muddy, Wet, Soft, Normal, Dry, Hard. */
#define SG_DOS_PITCH_TYPE_COUNT 7
/* The names in that order, lower case: "frozen" .. "hard" (the -type= values
 * and the file names' suffix). */
extern const char *const Sg_DosPitchTypeNames[SG_DOS_PITCH_TYPE_COUNT];

/* PitchTiledOptions. Sg_DosPitchTiledOptionsInit gives the C#'s defaults:
 * no blank tile, 256 pixels wide, crowd layers split, fans 1..22, no
 * derivation. SwosGfx's command line sets only the first two. */
typedef struct {
    int32_t insertBlankTile;    /* -insertblanktile: tile id 0 is all index 0 */
    int32_t tilesetWidth;       /* -tilesetwidth=N, pixels */
    int32_t splitCrowdLayers;
    int32_t firstFanPattern, lastFanPattern;
    int32_t fanRangeExplicit;
    int32_t deriveFans;
} SgDosPitchTiledOptions;

void    Sg_DosPitchTiledOptionsInit(SgDosPitchTiledOptions *o);

/* BuildPitchImage: the pitch as 672 x 880 indices (top row first) into
 * `indices`, and its palette (ARGB) into `palette`. A cell past a short pointer
 * table stays 0. 0 on success. */
int32_t Sg_DosPitchBuildImage(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                              uint8_t indices[SG_DOS_PITCH_W * SG_DOS_PITCH_H], uint32_t palette[256],
                              char *err);

/* SavePitchAsBmp: the whole pitch as an 8bpp BMP, reduced to `colorCount`
 * colours (1..256; the command line allows 16..256): indices at or above it go
 * to the nearest of the first colorCount (Sg_AmigaQuantizeToCount) and the
 * palette entries past it are written magenta, as SwosGfx's are. */
int32_t Sg_DosPitchSaveBmp(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                           const char *path, int32_t colorCount, char *err);

/* SavePitchAsTiled: `path` (.tmj) and the tileset BMP beside it, same name with
 * .bmp, reduced to colorCount as above. `options` NULL for the defaults.
 * SwosGfx's one warning -- fewer than half the stand cells are fans ("warning:
 * pitch6: only 183 of 420 stand cells are fan patterns 1..22 - ...", which it
 * writes to stderr) -- goes to `warn` (NULL for none). 0 on success. */
int32_t Sg_DosPitchSaveTiled(const SgDosPitchPatternSet *set, int32_t pitchIndex, int32_t pitchType,
                             const char *path, int32_t colorCount, const SgDosPitchTiledOptions *options,
                             SgLogFn warn, void *user, char *err);

/* The -dos pitch mode (Program.cs RunDos, DosMode.Pitch): load the patterns
 * from `dosDir`, then for each of the six pitches write
 * <outDir>/pitch<N>-<type>.bmp (tiled 0) or .tmj + .bmp (tiled 1). `outDir`
 * NULL or "" is the current folder. SwosGfx parses -pitch=N but this mode
 * writes all six whatever it says, so there is no pitch argument. On the first
 * failure it stops, with what was written so far left in place, as the C#
 * does. 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_DosPitchRun(const char *dosDir, const char *outDir, int32_t pitchType, int32_t colorCount,
                       int32_t tiled, int32_t insertBlankTile, int32_t tilesetWidth,
                       SgLogFn warn, void *user, char *err);

#endif
